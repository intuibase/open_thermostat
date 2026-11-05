"""Causal evaluation and safety checks for the offline-only learning variant."""
import copy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.compare_aos_learning import compile_replay, evaluate, replay, windows, schedule_warmups, predict_cycles, read_samples


class LearningComparisonTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='aos-replay-test-')
        cls.binary, _ = compile_replay(cls.directory.name)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def samples(self, count=37, slope=0, valid=True):
        return [dict(room_id='a0000001', room='Bedroom', boot=1,
                     ts=10000 + i * 300, sample_uptime_s=i * 300,
                     room_c=20 + i * slope, outdoor_c=10, actual_supply_c=42,
                     predicted_supply_c=42, sample_valid=valid, heat_delivered=True,
                     delay_min=10) for i in range(count)]

    def test_flat_heating_updates_candidate_but_not_baseline(self):
        rows = replay(self.samples(), self.binary)
        baseline, candidate = rows[-1]['baseline'], rows[-1]['candidate']
        self.assertEqual(baseline['heating_windows'], 0)
        self.assertGreater(candidate['heating_windows'], 0)
        self.assertLess(candidate['gain'], baseline['gain'])
        self.assertLess(candidate['fallback_c_per_h'], baseline['fallback_c_per_h'])
        self.assertGreaterEqual(candidate['fallback_c_per_h'], .1)

    def test_normal_warming_keeps_existing_behavior(self):
        rows = replay(self.samples(slope=.1), self.binary)
        self.assertGreater(rows[-1]['baseline']['heating_windows'], 0)
        for r in rows:
            self.assertEqual(r['baseline'], r['candidate'])

    def test_small_decline_during_confirmed_heating_can_update_gain(self):
        rows = replay(self.samples(slope=-.005), self.binary)
        self.assertEqual(rows[-1]['baseline']['heating_windows'], 0)
        self.assertGreater(rows[-1]['candidate']['heating_windows'], 0)
        self.assertLess(rows[-1]['candidate']['gain'], rows[-1]['baseline']['gain'])

    def test_invalid_samples_and_temperature_jumps_still_do_not_train(self):
        for rows in (self.samples(valid=False), self.samples(slope=1)):
            replay(rows, self.binary)
            for policy in ('baseline', 'candidate'):
                self.assertEqual(rows[-1][policy]['heating_windows'], 0)
                self.assertEqual(rows[-1][policy]['fallback_windows'], 0)

    def test_no_evaluation_window_crosses_gap_or_invalid_sample(self):
        rows = self.samples(count=25)
        for r in rows:
            r['delay_min'] = 0
        rows[10]['sample_valid'] = False
        for r in rows[19:]:
            r['sample_uptime_s'] += 400
            r['ts'] += 400
        selected = list(windows(rows))
        self.assertEqual(len(selected), 2)
        for w in selected:
            self.assertTrue(all(r['sample_valid'] for r in w))
            self.assertTrue(all(b['sample_uptime_s'] - a['sample_uptime_s'] <= 360
                                for a, b in zip(w, w[1:])))

    def test_duplicate_sample_is_an_explicit_replay_error(self):
        rows = self.samples(count=1) * 2
        with self.assertRaises(subprocess.CalledProcessError):
            replay(rows, self.binary)

    def test_frozen_model_never_uses_future_parameters(self):
        rows = self.samples(count=19)
        state = dict(gain=3, loss=.025, delay_min=0, fallback_c_per_h=1,
                     heating_windows=0, cooling_windows=0, fallback_windows=0,
                     reason='not_sampled')
        cutoff = rows[6]['ts']
        for r in rows:
            r['delay_min'] = 0
            r['baseline'] = dict(state)
            r['candidate'] = dict(state)
        before = evaluate(rows, cutoff)['a0000001']
        changed = copy.deepcopy(rows)
        for r in changed:
            if r['ts'] >= cutoff:
                r['candidate']['fallback_c_per_h'] = 5
        after = evaluate(changed, cutoff)['a0000001']
        self.assertEqual(before['frozen'], after['frozen'])
        self.assertEqual(before['validation_errors']['candidate_frozen_deployed'],
                         after['validation_errors']['candidate_frozen_deployed'])
        self.assertNotEqual(before['validation_errors']['candidate_online_deployed'],
                            after['validation_errors']['candidate_online_deployed'])

    def schedule_timeline(self):
        sample = replay(self.samples(count=1), self.binary)[0]
        sample['event'] = 'sample'
        snapshots = []
        for minute, temperature, target in [(0, 20, 20), (1, 20, 21), (2, 20.5, 21), (3, 20.96, 21)]:
            snapshots.append(dict(sample, event='snapshot', uptime_s=minute * 60,
                                  ts=10000 + minute * 60, room_c=temperature,
                                  scheduled_set_c=target, room_fresh=True, program='Comfort',
                                  margin_min=10, max_lead_min=240))
        return [sample, *snapshots]

    def test_schedule_cycles_use_goal_and_past_state_then_production_prediction(self):
        timeline = self.schedule_timeline()
        cycles = schedule_warmups(timeline, 0)
        self.assertEqual(len(cycles), 1)
        self.assertEqual(cycles[0]['target_c'], 21)
        self.assertEqual(cycles[0]['outcome'], 'reached')
        self.assertEqual(cycles[0]['reached_min'], 2)
        self.assertEqual(cycles[0]['states']['candidate'], timeline[0]['candidate'])
        predict_cycles(cycles, self.binary)
        self.assertGreater(cycles[0]['predictions']['candidate']['minutes'], 2)

    def test_schedule_setback_and_missing_room_censor_instead_of_claiming_arrival(self):
        for field, value, outcome in [('scheduled_set_c', 20, 'schedule_changed'),
                                      ('room_fresh', False, 'interrupted'),
                                      ('override_s', 600, 'interrupted')]:
            timeline = self.schedule_timeline()
            timeline[-1][field] = value
            cycles = schedule_warmups(timeline, 0)
            self.assertEqual(cycles[0]['outcome'], outcome)
            self.assertIsNone(cycles[0]['reached_min'])

    def test_decisions_observe_schedule_rise_and_brief_disturbance(self):
        timeline = self.schedule_timeline()
        timeline[2]['event'] = 'decision'  # first rising target
        cycles = schedule_warmups(timeline, 0)
        self.assertEqual(cycles[0]['reached_min'], 2)
        disturbance = dict(timeline[2], event='decision', uptime_s=90, ts=10090,
                           manual_test=True)
        timeline.insert(3, disturbance)
        cycles = schedule_warmups(timeline, 0)
        self.assertEqual(cycles[0]['outcome'], 'interrupted')
        self.assertIsNone(cycles[0]['reached_min'])

    def test_unconfirmed_delivery_does_not_calibrate(self):
        for supply, trustworthy in [(None, True), (22, True), (42, False)]:
            rows = self.samples(count=40)
            for i, r in enumerate(rows):
                r.update(room_fresh=True, room_c=20 + max(0, i - 2) / 36,
                         scheduled_set_c=20 if i < 2 else 21,
                         actual_supply_c=supply, heat_trustworthy=trustworthy)
            replay(rows, self.binary)
            self.assertEqual(rows[-1]['candidate']['warmup_windows'], 0)

    def test_decision_disturbance_resets_next_sample(self):
        records = [dict(v=1, boot=1, seq=1, event='decision', room_id='a0000001',
                        manual_test=True),
                   dict(v=1, boot=1, seq=2, event='sample', room_id='a0000001',
                        ts=10000, time_valid=True)]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'capture.log'
            path.write_text(''.join('AOS_DIAG ' + json.dumps(r) + '\n' for r in records))
            rows, errors = read_samples([path], ['a0000001'])
        self.assertFalse(errors)
        self.assertTrue(rows[0]['warmup_reset'])

    def test_shared_boost_between_samples_resets_replay(self):
        for field in ('manual_test', 'supply_boost_applied', 'supply_baseline_unknown'):
            records = [dict(v=1, boot=1, seq=1, event='decision', room_id='a0000001', **{field: True}),
                       dict(v=1, boot=1, seq=2, event='decision', room_id='a0000001', **{field: False}),
                       dict(v=1, boot=1, seq=3, event='sample', room_id='a0000001', ts=10000, time_valid=True)]
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / 'capture.log'
                path.write_text(''.join('AOS_DIAG ' + json.dumps(r) + '\n' for r in records))
                rows, errors = read_samples([path], ['a0000001'])
            self.assertFalse(errors)
            self.assertTrue(rows[0]['warmup_reset'])

    def test_shared_boost_prevents_warmup_but_keeps_basic_learning(self):
        rows = self.samples(count=40, slope=.03)
        for i, r in enumerate(rows):
            r.update(room_fresh=True, scheduled_set_c=20 if i < 2 else 22,
                     supply_boost_applied=True, actual_supply_c=70, predicted_supply_c=42)
        replay(rows, self.binary)
        self.assertEqual(rows[-1]['candidate']['warmup_windows'], 0)
        self.assertGreater(rows[-1]['candidate']['heating_windows'], 0)

    def test_unknown_supply_baseline_blocks_calibration_but_keeps_basic_learning(self):
        rows = self.samples(count=40, slope=.03)
        for i, r in enumerate(rows):
            r.update(room_fresh=True, scheduled_set_c=20 if i < 2 else 22,
                     supply_baseline_unknown=True, warmup_eligible=True,
                     actual_supply_c=70, predicted_supply_c=None)
        replay(rows, self.binary)
        self.assertEqual(rows[-1]['candidate']['warmup_windows'], 0)
        self.assertGreater(rows[-1]['candidate']['heating_windows'], 0)

    def test_explicit_boost_exclusion_prevents_calibration(self):
        rows = self.samples(count=40)
        for i, r in enumerate(rows):
            r.update(room_fresh=True, room_c=20 + max(0, i - 2) / 36,
                     scheduled_set_c=20 if i < 2 else 21,
                     actual_supply_c=70, predicted_supply_c=42,
                     warmup_eligible=i < 2)
        replay(rows, self.binary)
        self.assertEqual(rows[-1]['candidate']['warmup_windows'], 0)

    def test_replay_learns_whole_schedule_cycles_including_heat_pauses(self):
        rows = self.samples(count=400)
        for i, r in enumerate(rows):
            step = i % 100
            r['room_fresh'] = True
            r['room_c'] = 20 if step < 2 else min(21, 20 + (step - 2) / 36)
            r['scheduled_set_c'] = 20 if step < 2 else 21
            r['heat_delivered'] = step >= 2 and step % 5 != 0
        # Cooling/reset between artificial episodes must not jump >0.6°C in one sample.
        for start in (98, 198, 298):
            rows[start]['room_c'] = 20.5
            rows[start + 1]['room_c'] = 20
        replay(rows, self.binary)
        self.assertGreaterEqual(rows[-1]['candidate']['warmup_windows'], 3)
        self.assertEqual(rows[-1]['baseline']['warmup_windows'], 0)


if __name__ == '__main__':
    unittest.main()
