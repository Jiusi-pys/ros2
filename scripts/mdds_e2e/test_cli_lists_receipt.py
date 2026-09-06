"""Exercise host receipt rejection using a completed real board batch."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

SOURCE = Path(sys.argv.pop(1)).resolve()
VERIFY = Path(__file__).with_name('verify_cli_graph_basic.py').resolve()
BOARD = '3e01ff55454d202020104033bf453b00'


class ListReceipts(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='mdds_cli_receipt_')
        self.root = Path(self.temporary.name)
        for path in SOURCE.iterdir():
            if path.is_file() and (path.suffix in ('.json', '.log', '.pid') or path.name == 'nonce'):
                shutil.copyfile(path, self.root / path.name)
        self.path = self.root / (BOARD + '.cli.results.json')
        self.report = json.loads(self.path.read_text(encoding='utf-8'))

    def tearDown(self):
        self.temporary.cleanup()

    def run_verifier(self):
        self.path.write_text(json.dumps(self.report), encoding='utf-8')
        result = subprocess.run([sys.executable, '-B', str(VERIFY), str(self.root), self.report['run_id']], capture_output=True)
        return result.returncode

    def hidden(self):
        return next(r for r in self.report['results'] if r['case_id'] == 'cli:topic/list' and r['expected']['hidden'])

    def test_real_complete_batch(self): self.assertEqual(self.run_verifier(), 0)

    def test_missing_hidden(self):
        self.report['results'].remove(self.hidden())
        self.assertNotEqual(self.run_verifier(), 0)

    def test_duplicate_visible(self):
        self.hidden()['execution']['argv'].remove('--include-hidden-topics')
        self.assertNotEqual(self.run_verifier(), 0)

    def test_expected_mode_disagrees(self):
        self.hidden()['expected']['hidden'] = False
        self.assertNotEqual(self.run_verifier(), 0)

    def test_wrong_type_even_with_updated_hash(self):
        record = self.hidden()['execution']['log']
        path = self.root / (BOARD + '.' + record['path'])
        text = path.read_text(encoding='utf-8')
        self.assertIn('std_msgs/msg/Bool', text)
        path.write_text(text.replace('std_msgs/msg/Bool', 'std_msgs/msg/Int32'), encoding='utf-8')
        record['sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
        self.assertNotEqual(self.run_verifier(), 0)

    def test_missing_log(self):
        (self.root / (BOARD + '.' + self.hidden()['execution']['log']['path'])).unlink()
        self.assertNotEqual(self.run_verifier(), 0)

    def test_missing_isolation(self):
        self.report.pop('daemon_absence')
        self.assertNotEqual(self.run_verifier(), 0)

    def test_incomplete_isolation(self):
        self.report['daemon_absence'].pop()
        self.assertNotEqual(self.run_verifier(), 0)

    def test_existing_daemon(self):
        self.report['daemon_absence'][0]['daemons'] = [{'pid':123}]
        self.assertNotEqual(self.run_verifier(), 0)


if __name__ == '__main__': unittest.main()
