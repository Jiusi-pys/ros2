"""Adversarial structured tracing evidence tests, before board acceptance."""
import copy
import unittest
from trace_contract import validate_report, recipe, PHASES


class TraceReportTest(unittest.TestCase):
    def setUp(self):
        self.run, self.nonce, self.sha = 'run', 'a' * 32, 'b' * 64
        runtime = {'manifest_sha256': self.sha, 'files_verified': 50}
        self.value = {'run_id': self.run, 'nonce': self.nonce, 'role': 'A', 'passed': True,
                      'namespace': 'mnt:[2]', 'system_namespace': 'mnt:[1]', 'cleanup_remaining': [],
                      'namespace_cleanup': [{'pid': 99, 'start': '55'}], 'runtime_before': runtime,
                      'runtime_after': copy.deepcopy(runtime), 'commands': [], 'actors': {}}
        for index, (_, label, args, _) in enumerate(recipe(self.run, self.nonce)):
            self.value['commands'].append({'label': label.removeprefix('trace_'), 'returncode': 0, 'pid': 100 + index, 'start': '123',
                'argv': ['/data/python312-rk3588a/usr/bin/python3.12', '-u', '-B', '-c', 'from ros2cli.cli import main; raise SystemExit(main())'] + args})
        for index, phase in enumerate(PHASES):
            self.value['actors'][phase] = {'node': 'trace_' + phase + '_A', 'mount_namespace': 'mnt:[2]',
                'service': '/ros_broker_run/B/alpha/serve', 'a': int(self.nonce[:7], 16), 'b': index + 1,
                'sum': int(self.nonce[:7], 16) + index + 1, 'payload': '|'.join((self.run, self.nonce, 'A', phase)),
                'peer_ack': True, 'pid': 200 + index, 'start': '234', 'trace_mappings': {'Lib/libtracetools.so': self.sha}}

    def check(self): validate_report(self.value, self.run, self.nonce, 'A', self.sha)
    def reject(self):
        with self.assertRaises(ValueError): self.check()
    def test_complete(self): self.check()
    def test_failed_command(self): self.value['commands'][1]['returncode'] = 1; self.reject()
    def test_wrong_command(self): self.value['commands'][2]['argv'][-1] = 'other'; self.reject()
    def test_actor_failure(self): self.value['actors']['active']['sum'] = 0; self.reject()
    def test_missing_peer_ack(self): self.value['actors']['paused']['peer_ack'] = False; self.reject()
    def test_stale_payload(self): self.value['actors']['resumed']['payload'] = 'old'; self.reject()
    def test_missing_tracer_mapping(self): self.value['actors']['interactive']['trace_mappings'] = {}; self.reject()
    def test_runtime_drift(self): self.value['runtime_after']['manifest_sha256'] = 'c' * 64; self.reject()
    def test_runtime_not_checked(self): self.value['runtime_before']['files_verified'] = 0; self.reject()
    def test_cleanup_residual(self): self.value['cleanup_remaining'] = [{'pid': 99, 'start': '55'}]; self.reject()
    def test_cleanup_missing(self): del self.value['namespace_cleanup']; self.reject()

    def test_probe_without_receipts_is_rejected(self):
        with self.assertRaises(ValueError): validate_report({'passed': True}, 'run', 'a' * 32, 'A', 'b' * 64)

    def test_system_namespace_is_rejected(self):
        with self.assertRaises(ValueError):
            validate_report({'passed': True, 'namespace': 'mnt:[1]', 'system_namespace': 'mnt:[1]'}, 'run', 'a' * 32, 'A', 'b' * 64)


if __name__ == '__main__': unittest.main()
