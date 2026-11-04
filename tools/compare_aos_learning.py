#!/usr/bin/env python3
"""Replay corrected production AOS learning against historical device behavior.

The baseline restores the historical warming gate and does not learn scheduled
warmup calibration. The current model learns both on exactly the same samples.
Requires a C++17 compiler; otherwise Python standard library only.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
import math
from pathlib import Path
import statistics
import subprocess
import tempfile

try:
    from tools.analyze_aos_log import extract_records
except ModuleNotFoundError:
    from analyze_aos_log import extract_records

ROOT = Path(__file__).resolve().parents[1]
STATE_FIELDS = ('gain', 'loss', 'delay_min', 'fallback_c_per_h',
                'heating_windows', 'cooling_windows', 'fallback_windows',
                'warmup_rate', 'warmup_emission', 'warmup_delta', 'warmup_windows', 'warmup_physical_windows')


def compile_replay(directory):
    source = (ROOT / 'src/ThermalModel.h').read_text()
    gate = '\t\t\tif (rate < 0.1f)\n\t\t\t\treturn reject("insufficient_warming");\n'
    anchor = '\t\t\tstate.fallbackRate = std::clamp('
    if gate in source or source.count(anchor) != 1 or source.count('namespace heating::aos {') != 1:
        raise ValueError('Production model changed; review the experimental transformation')
    variant = source.replace(anchor, gate + anchor).replace('namespace heating::aos {',
                                                           'namespace aos_legacy {')
    directory = Path(directory)
    (directory / 'LegacyThermalModel.h').write_text(variant)
    binary = directory / 'replay'
    subprocess.run(['g++', '-std=c++17', '-O2', '-I', str(directory),
                    '-I', str(ROOT / 'src'), str(ROOT / 'tools/aos_learning_replay.cpp'),
                    '-o', str(binary)], check=True, capture_output=True, text=True)
    return binary, hashlib.sha256(source.encode()).hexdigest()


def confirmed_heat(r):
    if 'confirmed_heat' in r:
        return r['confirmed_heat']
    return (bool(r.get('heat_delivered')) and r.get('heat_trustworthy', True)
            and all(type(r.get(k)) in (int, float) and math.isfinite(r[k])
                    for k in ('room_c', 'actual_supply_c'))
            and r['actual_supply_c'] > r['room_c'] + 5)


def warmup_eligible(r):
    if 'warmup_eligible' in r:
        return r['warmup_eligible']
    delivered = bool(r.get('heat_delivered'))
    ordinary_pause = (not delivered and r.get('uses_ems')
                      and r.get('ems_heating') is False and not r.get('manual_test'))
    return (bool(r.get('room_fresh')) and r.get('time_valid', True)
            and not r.get('override_s') and not r.get('manual_test') and not r.get('apply')
            and r.get('room_c') is not None and -10 <= r['room_c'] <= 50
            and (r.get('heat_trustworthy', True) or ordinary_pause)
            and (not delivered or confirmed_heat(r)))


def replay(samples, binary):
    def number(value):
        return 'nan' if value is None else str(value)
    lines = []
    for r in samples:
        lines.append(' '.join([
            f"{r['boot']}_{r['room_id']}", str(r['sample_uptime_s']),
            *[number(r.get(k)) for k in ('room_c', 'outdoor_c', 'actual_supply_c')],
            str(int(r['heat_delivered'])), str(int(r['sample_valid'])),
            number(r.get('scheduled_set_c')), number(r.get('predicted_supply_c')),
            str(int(warmup_eligible(r))),
            str(int(r.get('warmup_reset', False))),
            str(int(confirmed_heat(r))),
        ]))
    result = subprocess.run([str(binary)], input='\n'.join(lines) + '\n',
                            capture_output=True, text=True, check=True)
    output = result.stdout.splitlines()
    if len(output) != len(samples):
        raise ValueError('Incomplete replay output')
    for r, line in zip(samples, output):
        fields = line.split()
        width = len(STATE_FIELDS) + 1
        for offset, name in ((0, 'baseline'), (width, 'candidate')):
            state = dict(zip(STATE_FIELDS, map(float, fields[offset:offset + len(STATE_FIELDS)])))
            state['reason'] = fields[offset + len(STATE_FIELDS)]
            r[name] = state
    return samples


def valid_runs(samples):
    """Use identical, observation-based evaluation windows for both models."""
    run = []
    for r in samples:
        ok = (r.get('sample_valid') is True and r.get('heat_delivered') is True
              and all(type(r.get(k)) in (float, int) and math.isfinite(r[k]) for k in
                      ('room_c', 'outdoor_c', 'actual_supply_c', 'predicted_supply_c', 'delay_min')))
        if run:
            p = run[-1]
            continuous = (ok and r['boot'] == p['boot']
                          and 299 <= r['sample_uptime_s'] - p['sample_uptime_s'] <= 360
                          and abs(r['room_c'] - p['room_c']) <= .6)
            if continuous:
                continuous = abs((r['ts'] - p['ts']) -
                                 (r['sample_uptime_s'] - p['sample_uptime_s'])) <= 90
            if not continuous:
                yield run
                run = []
        if ok:
            run.append(r)
    if run:
        yield run


def windows(samples):
    for run in valid_runs(samples):
        for i in range(0, len(run) - 6, 6):
            w = run[i:i + 7]
            duration = w[-1]['sample_uptime_s'] - w[0]['sample_uptime_s']
            if (1800 <= duration <= 2100 and
                    w[0]['sample_uptime_s'] - run[0]['sample_uptime_s'] >= w[0]['delay_min'] * 60):
                yield w


def forecast(window, state, kind):
    temperature = window[0]['room_c']
    hours = (window[-1]['sample_uptime_s'] - window[0]['sample_uptime_s']) / 3600
    ready = state['heating_windows'] >= 12 and state['cooling_windows'] >= 6
    if kind == 'deployed':
        kind = 'calibrated' if state.get('warmup_windows', 0) >= 3 else 'physical' if ready else 'fallback'
    if kind == 'fallback':
        return temperature + state['fallback_c_per_h'] * hours
    supply, outdoor = window[0]['predicted_supply_c'], window[0]['outdoor_c']
    steps = math.ceil(hours * 60)
    for _ in range(steps):
        emission = (max(0, supply - temperature - 5) / 50) ** 1.3
        rate = state['gain'] * emission - state['loss'] * (temperature - outdoor)
        if kind == 'calibrated':
            rate = state['warmup_rate']
            if state['warmup_physical_windows'] == state['warmup_windows']:
                rate += state['gain'] * (emission - state['warmup_emission']) - state['loss'] * (temperature - outdoor - state['warmup_delta'])
        temperature += rate * hours / steps
    return temperature


def metrics(errors):
    return {'n': len(errors), 'mae_c': statistics.mean(map(abs, errors)),
            'bias_c': statistics.mean(errors)} if errors else {'n': 0}


def evaluate(samples, cutoff):
    by_room = defaultdict(list)
    for r in samples:
        by_room[r['room_id']].append(r)
    result = {}
    for rid, rows in by_room.items():
        # A frozen model is never taken from inside the validation window.
        prior = [r for r in rows if r['ts'] < cutoff]
        if not prior:
            continue
        frozen = prior[-1]
        evaluation = [w for w in windows(rows) if w[0]['ts'] >= cutoff]
        errors = defaultdict(list)
        ready = Counter()
        for w in evaluation:
            actual = w[-1]['room_c']
            errors['constant'].append(w[0]['room_c'] - actual)
            for policy in ('baseline', 'candidate'):
                for mode, state in (('online', w[0][policy]), ('frozen', frozen[policy])):
                    label = f'{policy}_{mode}'
                    ready[label] += state['heating_windows'] >= 12 and state['cooling_windows'] >= 6
                    for kind in ('physical', 'fallback', 'deployed'):
                        errors[f'{label}_{kind}'].append(forecast(w, state, kind) - actual)
        fidelity = Counter()
        max_difference = defaultdict(float)
        for r in rows:
            if r['baseline']['reason'] != r.get('observation'):
                fidelity['observation_mismatches'] += 1
            for field in STATE_FIELDS:
                if type(r.get(field)) in (float, int):
                    difference = abs(r['baseline'][field] - r[field])
                    max_difference[field] = max(max_difference[field], difference)
                    if field.endswith('_windows') and difference:
                        fidelity['counter_mismatches'] += 1
        result[rid] = {
            'room': rows[0].get('room', rid), 'samples': len(rows),
            'baseline_replay_fidelity': dict(fidelity),
            'baseline_max_parameter_difference_from_log': dict(max_difference),
            'baseline_replay_matches_log': not fidelity and bool(max_difference)
                and all(difference < 1e-4 for difference in max_difference.values()),
            'training_settled_windows': sum(w[-1]['ts'] < cutoff for w in windows(rows)),
            'validation_settled_windows': len(evaluation),
            'ready_windows': dict(ready),
            'frozen': {p: frozen[p] for p in ('baseline', 'candidate')},
            'latest': {p: rows[-1][p] for p in ('baseline', 'candidate')},
            'validation_errors': {name: metrics(values) for name, values in errors.items()},
            'learning_outcomes': {p: dict(Counter(r[p]['reason'] for r in rows))
                                  for p in ('baseline', 'candidate')},
        }
    return result


def read_samples(paths, rooms, timeline=None):
    seen, samples, errors = set(), [], []
    reset_warmup = set()
    for r, error in extract_records(paths):
        if error:
            errors.append(error)
            continue
        key = r['boot'], r['seq']
        if key in seen or r['room_id'] not in rooms:
            continue
        seen.add(key)
        if timeline is not None:
            timeline.append(r)
        if r['event'] in ('reset', 'reload'):
            raise ValueError('Reset/reload replay is unsupported; use an uninterrupted capture')
        if (r['event'] == 'clock_changed' or r.get('warmup_reset')
                or r.get('warmup_eligible') is False
                or r.get('override_s') or r.get('manual_test') or r.get('apply')
                or r.get('room_fresh') is False):
            reset_warmup.add((r['boot'], r['room_id']))
        if r['event'] == 'sample':
            if not r.get('time_valid') or r.get('ts') is None:
                raise ValueError('Chronological validation requires a valid wall clock')
            reset_key = r['boot'], r['room_id']
            r['warmup_reset'] = reset_key in reset_warmup
            reset_warmup.discard(reset_key)
            samples.append(r)
    if not samples:
        raise ValueError('No learning samples for the requested rooms')
    return samples, errors


def schedule_warmups(timeline, cutoff):
    """Observe actual schedule increases, ending observation on disruption/setback.

    Temperature/boiler inputs are observed, not a counterfactual preheating replay.
    Only states learned by the start snapshot may forecast that cycle.
    """
    latest, previous, active, completed = {}, {}, {}, []

    def finish(key, reason):
        c = active.pop(key)
        last, start = c['last'], c['start']
        c['outcome'] = reason
        c['observed_min'] = (last['uptime_s'] - start['uptime_s']) / 60
        c['reached_min'] = c['observed_min'] if reason == 'reached' else None
        completed.append(c)

    for r in timeline:
        key = r['boot'], r['room_id']
        if r['event'] == 'sample':
            latest[key] = r
            continue
        if r['event'] not in ('snapshot', 'decision') or r.get('ts') is None:
            continue
        p = previous.get(key)
        previous[key] = r
        valid = (r.get('room_fresh') is True and type(r.get('room_c')) in (int, float)
                 and not r.get('override_s') and not r.get('manual_test') and not r.get('apply'))
        continuity = (p is not None and r.get('program') == p.get('program')
                      and 0 < r['uptime_s'] - p['uptime_s'] <= 180
                      and abs((r['ts'] - p['ts']) - (r['uptime_s'] - p['uptime_s'])) <= 90)
        if key in active:
            c = active[key]
            if not valid or not continuity:
                finish(key, 'interrupted')
            elif r['scheduled_set_c'] != c['target_c']:
                finish(key, 'schedule_changed')
            else:
                c['last'] = r
                c['high_supply'] |= (type(r.get('actual_supply_c')) in (int, float)
                                     and type(r.get('predicted_supply_c')) in (int, float)
                                     and r['actual_supply_c'] > r['predicted_supply_c'] + 10)
                if r['room_c'] >= c['target_c'] - .05:
                    finish(key, 'reached')
        if (valid and continuity and p.get('room_fresh') and key in latest
                and r['scheduled_set_c'] > p['scheduled_set_c']
                and r['room_c'] < r['scheduled_set_c'] - .05 and r['ts'] >= cutoff
                and all(type(r.get(f)) in (int, float) and math.isfinite(r[f])
                        for f in ('outdoor_c', 'predicted_supply_c'))):
            active[key] = {'room_id': r['room_id'], 'room': r.get('room'),
                           'start': r, 'last': r, 'target_c': r['scheduled_set_c'],
                           'states': {k: latest[key][k] for k in ('baseline', 'candidate')},
                           'high_supply': False}
    for key in list(active):
        finish(key, 'end_of_log')
    return completed


def predict_cycles(cycles, binary):
    requests, order = [], []
    for c in cycles:
        for name in ('baseline', 'candidate'):
            s, r = c['states'][name], c['start']
            requests.append(' '.join(str(v) for v in (
                s['gain'], s['loss'], s['delay_min'], s['fallback_c_per_h'],
                int(s['heating_windows']), int(s['cooling_windows']),
                s['warmup_rate'], s['warmup_emission'], s['warmup_delta'],
                int(s['warmup_windows']), int(s['warmup_physical_windows']),
                r['room_c'], c['target_c'] - .05, r['outdoor_c'], r['predicted_supply_c'])))
            order.append((c, name))
    if not requests:
        return
    result = subprocess.run([str(binary), '--predict'], input='\n'.join(requests) + '\n',
                            text=True, capture_output=True, check=True)
    output = result.stdout.splitlines()
    if len(output) != len(order):
        raise ValueError('Incomplete forecast output')
    for (c, name), line in zip(order, output):
        minutes, fallback, unreachable = map(int, line.split())
        c.setdefault('predictions', {})[name] = {
            'minutes': minutes, 'fallback': bool(fallback), 'unreachable': bool(unreachable),
            'exceeds_lead_limit': bool(unreachable) or minutes + c['start']['margin_min'] > c['start']['max_lead_min'],
        }


def summarize_cycles(cycles):
    rooms = {}
    for rid in sorted({c['room_id'] for c in cycles}):
        selected = [c for c in cycles if c['room_id'] == rid and c['observed_min'] >= 30]
        result = {'room': next(c['room'] for c in cycles if c['room_id'] == rid),
                  'cycles_at_least_30_min': len(selected),
                  'outcomes': dict(Counter(c['outcome'] for c in selected)),
                  'high_supply_cycles': sum(c['high_supply'] for c in selected)}
        for name in ('baseline', 'candidate'):
            errors = [c['predictions'][name]['minutes'] - c['reached_min'] for c in selected
                      if c['outcome'] == 'reached' and not c['predictions'][name]['unreachable']]
            result[name] = {
                'reached_only': {'n': len(errors), 'mae_min': statistics.mean(map(abs, errors)),
                                 'bias_predicted_minus_actual_min': statistics.mean(errors)} if errors else {'n': 0},
                'censored_but_predicted_time_expired': sum(
                    c['outcome'] != 'reached' and not c['predictions'][name]['unreachable']
                    and c['predictions'][name]['minutes'] <= c['observed_min'] for c in selected),
                'unreachable': sum(c['predictions'][name]['unreachable'] for c in selected),
                'exceeds_lead_limit': sum(c['predictions'][name]['exceeds_lead_limit'] for c in selected),
            }
        rooms[rid] = result
    return rooms


def main():
    from datetime import datetime
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs', nargs='+', type=Path)
    parser.add_argument('--validation-from', required=True, help='ISO datetime with timezone')
    parser.add_argument('--room-id', action='append', dest='rooms', required=True,
                        help='room ID to evaluate; repeat for multiple rooms')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    cutoff = datetime.fromisoformat(args.validation_from)
    if cutoff.tzinfo is None:
        parser.error('--validation-from must include a timezone offset')
    timeline = []
    samples, errors = read_samples(args.logs, args.rooms, timeline)
    with tempfile.TemporaryDirectory(prefix='aos-learning-') as directory:
        binary, digest = compile_replay(directory)
        replay(samples, binary)
        cycles = schedule_warmups(timeline, cutoff.timestamp())
        predict_cycles(cycles, binary)
    report = {'experiment': 'production_warmup_calibration_vs_legacy',
              'production_header_sha256': digest, 'validation_from': cutoff.isoformat(),
              'malformed_records': errors, 'rooms': evaluate(samples, cutoff.timestamp()),
              'schedule_warmups': summarize_cycles(cycles),
              'schedule_cycles': [{k: v for k, v in c.items() if k not in ('start', 'last', 'states')}
                                  | {'start_ts': c['start']['ts'], 'initial_room_c': c['start']['room_c'],
                                     'outdoor_c': c['start']['outdoor_c'],
                                     'predicted_supply_c': c['start']['predicted_supply_c']}
                                  for c in cycles],
              'limitations': ['Observed samples, not full controller replay',
                              'Deployed means readiness selection for a settled window, not schedule arrival',
                              'Frozen states use only pre-cutoff data; online states use prior validation data',
                              'Diagnostic physical forecasts can precede model readiness',
                              'Historical data were already examined; results do not measure active preheating arrival']}
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    for room in report['rooms'].values():
        print(f"{room['room']}: {room['validation_settled_windows']} validation windows")
        for name in ('baseline_frozen_deployed', 'candidate_frozen_deployed',
                     'baseline_online_deployed', 'candidate_online_deployed', 'constant'):
            print(f"  {name}: {room['validation_errors'].get(name, {'n': 0})}")
        print('  Replay mismatches:', room['baseline_replay_fidelity'])
    if not all(r['baseline_replay_matches_log'] for r in report['rooms'].values()):
        raise SystemExit('Baseline replay differs from the log; do not treat this comparison as validated')


if __name__ == '__main__':
    main()
