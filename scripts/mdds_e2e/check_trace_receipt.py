"""Adversarial checks against a complete real-board tracing evidence bundle."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
import cli_acceptance as a
from verify_cli_daemon import validate_report

SOURCE = Path(sys.argv[1]).resolve()
A, B = a.TARGET['board_serials']


class TraceReceiptTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='trace_receipt_')
        self.root = Path(self.temp.name) / SOURCE.name
        shutil.copytree(SOURCE, self.root, ignore=shutil.ignore_patterns('*.tar', '*.py', 'mdds_broker_daemon', 'mdds_token_exec'))
        self.value = json.loads((self.root / (A + '.cli.results.json')).read_bytes())
        self.nonce = (self.root / 'nonce').read_text().strip()
    def tearDown(self): self.temp.cleanup()
    def check(self): validate_report(self.value, self.root, SOURCE.name, A, self.nonce)
    def reject(self):
        with self.assertRaises((ValueError, AssertionError)): self.check()
    def test_original(self): self.check()
    def test_runtime_changed(self): self.value['trace_probe']['runtime_after']['manifest_sha256'] = '0' * 64; self.reject()
    def test_global_namespace(self): self.value['trace_probe']['namespace'] = self.value['trace_probe']['system_namespace']; self.reject()
    def test_command_failed(self): self.value['trace_probe']['commands'][1]['returncode'] = 1; self.reject()
    def test_stale_session(self): self.value['trace_probe']['commands'][0]['argv'][7] = 'stale'; self.reject()
    def test_unfinished_cleanup(self): self.value['trace_probe']['cleanup_remaining'] = [{'pid': 1}]; self.reject()
    def test_missing_actor(self): del self.value['trace_probe']['actors']['paused']; self.reject()
    def test_wrong_peer_ack(self): self.value['trace_probe']['actors']['stopped']['peer_ack'] = False; self.reject()
    def test_wrong_actor_pid(self): self.value['trace_probe']['actors']['active']['pid'] += 1; self.reject()
    def test_missing_peer_payload(self):
        p = self.root / (B + '.trace_received.json'); v = json.loads(p.read_bytes()); v['received'].pop(); p.write_text(json.dumps(v)); self.reject()
    def test_rehashed_missing_events(self):
        folder = self.root / (A + '.trace_decoded'); p = folder / 'lifecycle.decoded.log'; p.write_bytes(b'')
        proof = folder / 'verification.json'; v = json.loads(proof.read_bytes()); v['decoded_sha256']['lifecycle'] = a.digest(b''); proof.write_text(json.dumps(v)); self.reject()
    def test_archive_changed(self):
        p = self.root / (A + '.trace_probe.tar.gz'); p.write_bytes(p.read_bytes() + b'changed'); self.reject()
    def test_rehashed_terminal_missing(self):
        e = next(r['execution'] for r in self.value['results'] if r['label'] == 'trace_pause')
        p = self.root / (A + '.' + e['log']['path']); raw = p.read_text().replace('MDDS_CLI_TERMINAL ', 'REMOVED ')
        p.write_text(raw, newline='\n'); e['log']['sha256'] = a.digest(p.read_bytes()); self.reject()
    def test_rehashed_interactive_output(self):
        e = next(r['execution'] for r in self.value['results'] if r['label'] == 'trace_interactive')
        p = self.root / (A + '.' + e['log']['path']); raw = p.read_text().replace('press enter to stop...', 'wrong prompt')
        p.write_text(raw, newline='\n'); e['log']['sha256'] = a.digest(p.read_bytes()); self.reject()


if __name__ == '__main__': unittest.main(argv=[sys.argv[0]])
