#!/usr/bin/env python3
"""Extract versioned AOS_DIAG records from mixed serial/TCP logs (stdlib only)."""
import argparse
import csv
import gzip
import json
from collections import Counter
from pathlib import Path

MARKER = 'AOS_DIAG '


def extract_records(paths):
    """Yield (record, error); logger timestamp/feature prefixes are ignored."""
    for path in paths:
        opener = gzip.open if str(path).endswith('.gz') else open
        with opener(path, 'rt', encoding='utf-8', errors='replace') as source:
            for number, line in enumerate(source, 1):
                if MARKER not in line:
                    continue
                location = f'{path}:{number}'
                try:
                    record = json.loads(line.split(MARKER, 1)[1])
                    if not isinstance(record, dict) or record.get('v') != 1:
                        raise ValueError('unsupported diagnostic schema')
                    if (not isinstance(record.get('room_id'), str)
                            or not isinstance(record.get('event'), str)
                            or type(record.get('boot')) is not int
                            or type(record.get('seq')) is not int):
                        raise ValueError('missing record identity')
                    yield record, None
                except (ValueError, TypeError) as error:
                    yield None, f'{location}: {error}'


def analyze(paths, csv_path=None):
    records, errors, seen = [], [], set()
    duplicates = 0
    for record, error in extract_records(paths):
        if error:
            errors.append(error)
            continue
        identity = record['boot'], record['seq']
        if identity in seen:
            duplicates += 1
            continue
        seen.add(identity)
        records.append(record)
    if csv_path is not None:
        fields = sorted({key for record in records for key in record})
        with open(csv_path, 'w', newline='', encoding='utf-8') as output:
            writer = csv.DictWriter(output, fieldnames=fields)
            writer.writeheader()
            writer.writerows(records)
    rooms, sequences = {}, {}
    for record in records:
        identity = record['room_id']
        room = rooms.setdefault(identity, {
            'name': record.get('room', identity), 'records': 0, 'boots': set(),
            'samples': 0, 'invalid_samples': 0, 'accepted_windows': 0,
            'learning_reasons': Counter(), 'sample_reasons': Counter(),
            'events': Counter(), 'status_records': Counter(),
            'minimum_room_c': None, 'maximum_room_c': None,
            'first_ts': None, 'last_ts': None, 'latest': {},
        })
        room['records'] += 1
        room['boots'].add(record['boot'])
        room['events'][record['event']] += 1
        if record.get('status'):
            room['status_records'][record['status']] += 1
        if record['event'] == 'sample':
            room['samples'] += 1
            room['invalid_samples'] += record.get('sample_valid') is False
            room['accepted_windows'] += record.get('window_accepted') is True
            room['learning_reasons'][record.get('observation', 'unknown')] += 1
            room['sample_reasons'][record.get('sample_reason', 'unknown')] += 1
        temp = record.get('room_c')
        if type(temp) in (float, int):
            room['minimum_room_c'] = temp if room['minimum_room_c'] is None else min(room['minimum_room_c'], temp)
            room['maximum_room_c'] = temp if room['maximum_room_c'] is None else max(room['maximum_room_c'], temp)
        timestamp = record.get('ts')
        if type(timestamp) in (float, int):
            room['first_ts'] = timestamp if room['first_ts'] is None else min(room['first_ts'], timestamp)
            room['last_ts'] = timestamp if room['last_ts'] is None else max(room['last_ts'], timestamp)
        room['latest'] = record
        sequences.setdefault(record['boot'], set()).add(record['seq'])
    for room in rooms.values():
        room['boots'] = sorted(room['boots'])
    gaps = {str(boot): max(values) - min(values) + 1 - len(values)
            for boot, values in sequences.items()}
    return {
        'records': len(records), 'duplicates_ignored': duplicates,
        'malformed_records': errors, 'sequence_gaps_within_dump': gaps, 'rooms': rooms,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs', nargs='+', type=Path, help='full device logs, optionally .gz')
    parser.add_argument('--csv', type=Path, help='write all unique records as CSV')
    parser.add_argument('--summary-json', type=Path, help='write full machine-readable summary')
    args = parser.parse_args()
    summary = analyze(args.logs, args.csv)
    if args.summary_json:
        args.summary_json.write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding='utf-8')
    print(f"AOS records: {summary['records']}; duplicates: {summary['duplicates_ignored']}; "
          f"malformed/unsupported: {len(summary['malformed_records'])}")
    for identity, room in summary['rooms'].items():
        latest = room['latest']
        print(f"\n{room['name']} ({identity}), boots: {len(room['boots'])}")
        print(f"  Samples: {room['samples']}, invalid: {room['invalid_samples']}, accepted windows in dump: {room['accepted_windows']}")
        print(f"  Temperature range: {room['minimum_room_c']} .. {room['maximum_room_c']} C")
        print(f"  Latest: {latest.get('status')}, coverage={latest.get('coverage_pct')}%, "
              f"gain={latest.get('gain')}, loss={latest.get('loss')}, fallback={latest.get('fallback')}")
        print('  Learning outcomes:', dict(room['learning_reasons']))
        print('  Sample quality:', dict(room['sample_reasons']))
        print('  Events:', dict(room['events']))
    gaps = sum(summary['sequence_gaps_within_dump'].values())
    if gaps:
        print(f'\nMissing sequence numbers inside dump: {gaps} (lost/filtered records or incomplete input).')
    for error in summary['malformed_records'][:10]:
        print('WARNING:', error)
    if not summary['records']:
        print('No AOS_DIAG records. Enable the AOS logging feature on firmware supporting schema v1.')
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
