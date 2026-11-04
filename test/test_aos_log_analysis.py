"""Offline log extraction; does not require Home Assistant."""
import csv
import gzip
import json
import tempfile
import unittest
from pathlib import Path
from tools.analyze_aos_log import analyze


class AosLogAnalysisTest(unittest.TestCase):
    def record(self, seq=1, boot=10, **fields):
        return dict(v=1, boot=boot, seq=seq, event='sample', room_id='aabbccdd',
                    room='Łazienka', sample_valid=True, room_c=20.1, **fields)

    def test_mixed_log_duplicates_restarts_nulls_and_csv(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'device.log'
            first = self.record(observation='heating_accepted', window_accepted=True, ts=100)
            missing = self.record(seq=3, observation='invalid_sample', sample_reason='ems_activity_missing', ts=160)
            missing['sample_valid'] = False
            missing['room_c'] = None
            restarted = self.record(boot=20, ts=200)
            path.write_text('ordinary log\n' + '\n'.join(
                '[AOS       ] AOS_DIAG ' + json.dumps(record)
                for record in [first, first, missing, restarted]), encoding='utf-8')
            csv_path = Path(directory) / 'samples.csv'
            summary = analyze([path], csv_path)
            self.assertEqual(summary['records'], 3)
            self.assertEqual(summary['duplicates_ignored'], 1)
            self.assertEqual(summary['sequence_gaps_within_dump']['10'], 1)
            room = summary['rooms']['aabbccdd']
            self.assertEqual(room['boots'], [10, 20])
            self.assertEqual(room['invalid_samples'], 1)
            self.assertEqual(room['accepted_windows'], 1)
            self.assertEqual(room['minimum_room_c'], 20.1)
            self.assertEqual(room['sample_reasons']['ems_activity_missing'], 1)
            with csv_path.open(encoding='utf-8') as stream:
                rows = list(csv.DictReader(stream))
            self.assertEqual(rows[1]['room_c'], '')
            self.assertEqual(rows[0]['room'], 'Łazienka')

    def test_malformed_truncated_unknown_schema_and_gzip(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'device.log.gz'
            with gzip.open(path, 'wt', encoding='utf-8') as stream:
                stream.write('AOS_DIAG {"v":1\nAOS_DIAG {"v":2}\n')
                stream.write('AOS_DIAG ' + json.dumps(self.record()))
            summary = analyze([path])
            self.assertEqual(summary['records'], 1)
            self.assertEqual(len(summary['malformed_records']), 2)


if __name__ == '__main__':
    unittest.main()
