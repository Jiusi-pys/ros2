"""Host-only fail-closed harness oracles; no ROS imports or board access."""
import copy
import hashlib
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest

from type_description_lifetime import find_native, pack_rclpy, extract_rclpy, validate_result

NATIVE = '_rclpy_pybind11.cpython-312-aarch64-linux-ohos.so'
CASES = {
 'test_last_native_impl_copy_owns_service_until_explicit_release':
   [('before_copy', 1), ('copy_retains_service', 1), ('after_last_copy_release', 0)],
 'test_node_destroy_retires_type_description_service_and_both_endpoints':
   [('before_node_destroy', 1), ('after_node_destroy', 0)],
}

def elf(payload=b'old'):
    header = bytearray(64)
    header[:6] = b'\x7fELF\x02\x01'
    header[18:20] = (183).to_bytes(2, 'little')
    return bytes(header) + payload

class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.package = self.root / 'rclpy'
        (self.package / 'impl').mkdir(parents=True)
        (self.package / '__init__.py').write_text('ROOT = True\n')
        (self.package / 'impl/__init__.py').write_text('NESTED = True\n')
        (self.package / 'py.typed').write_bytes(b'')
        (self.package / NATIVE).write_bytes(elf())
        self.archive = self.root / 'overlay.tar'

    def test_complete_package_uses_selected_native_with_original_import_suffix(self):
        native = self.root / NATIVE
        native.write_bytes(elf(b'new'))
        manifest = pack_rclpy(self.package, native, self.archive)
        self.assertEqual(self.package / NATIVE, find_native(self.package))
        extract_rclpy(self.archive, manifest, self.root / 'python')
        self.assertEqual(elf(b'new'), (self.root / 'python/rclpy' / NATIVE).read_bytes())
        self.assertEqual('NESTED = True\n', (self.root / 'python/rclpy/impl/__init__.py').read_text())
        self.assertTrue((self.root / 'python/rclpy/py.typed').is_file())

    def test_missing_or_ambiguous_native_and_wrong_architecture_are_rejected(self):
        native = self.package / NATIVE
        native.unlink()
        with self.assertRaises(ValueError): find_native(self.package)
        native.write_bytes(elf())
        (self.package / '_rclpy_pybind11.extra.so').write_bytes(elf())
        with self.assertRaises(ValueError): find_native(self.package)
        (self.package / '_rclpy_pybind11.extra.so').unlink()
        native.write_bytes(b'Windows or wrong ABI')
        with self.assertRaises(ValueError): pack_rclpy(self.package, native, self.archive)

    def test_archive_hash_and_member_hash_are_both_enforced(self):
        manifest = pack_rclpy(self.package, self.package / NATIVE, self.archive)
        with self.archive.open('ab') as output: output.write(b'changed')
        with self.assertRaises(ValueError): extract_rclpy(self.archive, manifest, self.root / 'bad')
        manifest['archive_sha256'] = hashlib.sha256(self.archive.read_bytes()).hexdigest()
        manifest['files']['rclpy/__init__.py']['sha256'] = '0' * 64
        with self.assertRaises(ValueError): extract_rclpy(self.archive, manifest, self.root / 'bad2')

    def test_path_traversal_and_links_are_rejected_before_any_write(self):
        for link in (False, True):
            archive = self.root / ('link.tar' if link else 'escape.tar')
            name = 'rclpy/link.py' if link else '../outside.py'
            with tarfile.open(archive, 'w') as output:
                member = tarfile.TarInfo(name)
                if link:
                    member.type = tarfile.SYMTYPE
                    member.linkname = '../../outside'
                    output.addfile(member)
                else:
                    member.size = 1
                    output.addfile(member, io.BytesIO(b'x'))
            manifest = {'schema_version': 1, 'archive_sha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
                        'native_name': NATIVE, 'native_sha256': '0' * 64,
                        'files': {name: {'sha256': hashlib.sha256(b'x').hexdigest(), 'bytes': 0 if link else 1}}}
            with self.assertRaises(ValueError): extract_rclpy(archive, manifest, self.root / ('bad_link' if link else 'bad_path'))
        self.assertFalse((self.root.parent / 'outside.py').exists())

class ReceiptTests(unittest.TestCase):
    def setUp(self):
        self.root = '/data/local/tmp/ros2/.mdds-owned-runs/td_test'
        self.plan = {'schema_version': 1, 'run_id': 'td_test', 'domain': 51,
                     'package': {'native_name': NATIVE, 'native_sha256': '1' * 64},
                     'libraries': {'libmdds.so': '2' * 64, 'librmw_mdds.so': '3' * 64},
                     'fixture_sha256': '4' * 64}
        self.status = {'schema_version': 1, 'run_id': 'td_test', 'role': 'type_lifetime',
                       'namespace': '/type_lifetime_td_test', 'child_pid': 123, 'child_start': '456', 'returncode': 0}
        self.record = 'MDDS_OWNED_PROCESS RUN_ID=td_test TAG=type_lifetime_child PID=123 START=456\n'
        files = {name: {'path': self.root + '/lib/' + name, 'sha256': sha} for name, sha in self.plan['libraries'].items()}
        files[NATIVE] = {'path': self.root + '/python/rclpy/' + NATIVE, 'sha256': '1' * 64}
        provenance = {'pid': 123, 'rmw': 'rmw_mdds', 'files': files,
                      'rclpy_package': self.root + '/python/rclpy/__init__.py',
                      'profile': 'ohos_dsoftbus', 'domain': '51', 'discovery_range': 'SYSTEM_DEFAULT',
                      'legacy_transport': None, 'broker_socket': None}
        self.result = {'schema_version': 1, 'run_id': 'td_test', 'pid': 123, 'fixture_exit': 0,
                       'fixture_sha256': '4' * 64, 'cross_board_proven': False,
                       'scope': 'same-context-node-lifetime', 'before': provenance, 'after': copy.deepcopy(provenance)}
        self.events = []
        for case, phases in CASES.items():
            for phase, count in phases:
                self.events.append({'case': case, 'phase': phase, 'expected_count': count, 'matched': True,
                  'service_name': f'/rclpy_type_lifetime_123/{case}/alpha/get_type_description',
                  'context_ok': True, 'node_names': ['alpha', 'observer'] if count else ['observer'],
                  'service_count': count, 'request_readers': count, 'response_writers': count,
                  'service_types': ['type_description_interfaces/srv/GetTypeDescription'] if count else [],
                  'request_gids': [[1, 2, 3]] if count else [], 'response_gids': [[4, 5, 6]] if count else [],
                  'observer_service_count': 1})

    def log(self):
        return ('mdds transports active: dsoftbus(test)\n' * 2 +
          ''.join('test (...) ... TYPE_DESCRIPTION_LIFETIME_OBSERVED ' + json.dumps(e) + '\n' for e in self.events) +
          'Ran 2 tests in 0.010s\n\nOK\n' +
          'TYPE_DESCRIPTION_RUN_RESULT ' + json.dumps(self.result) + '\n' +
          'GRAPH_PROCESS_EXIT ' + json.dumps(self.status) + '\n')

    def check(self, log=None):
        return validate_result(self.log() if log is None else log, self.status, self.record, self.plan, self.root)

    def test_exact_real_wait_and_five_functional_phases_pass(self):
        report = self.check()
        self.assertTrue(report['valid_evidence'])
        self.assertTrue(report['passed'])

    def test_printed_ok_cannot_replace_nonzero_real_exit_or_pid_identity(self):
        self.status['returncode'] = 1
        self.assertFalse(self.check()['passed'])
        self.status['returncode'] = 0
        self.status['child_pid'] = 999
        self.assertFalse(self.check()['valid_evidence'])

    def test_shared_or_wrong_native_overlay_and_hash_are_rejected(self):
        for field, value in [('path', '/shared/' + NATIVE), ('sha256', '0' * 64)]:
            with self.subTest(field=field):
                result = copy.deepcopy(self.result)
                self.result['after']['files'][NATIVE][field] = value
                self.assertFalse(self.check()['valid_evidence'])
                self.result = result

    def test_real_lifetime_red_is_complete_evidence_but_never_a_pass(self):
        self.status['returncode'] = 1
        self.result['fixture_exit'] = 1
        for event in self.events:
            if event['expected_count'] == 0:
                event.update(matched=False, service_count=1, request_readers=1,
                             response_writers=1, node_names=['alpha', 'observer'],
                             request_gids=[[1, 2, 3]], response_gids=[[4, 5, 6]])
        report = self.check(self.log().replace('\nOK\n', '\nFAILED (failures=2)\n'))
        self.assertTrue(report['valid_evidence'])
        self.assertFalse(report['passed'])
        self.assertTrue(report['functional_errors'])
        self.assertFalse(self.check('Permission failure before Context.init\n')['valid_evidence'])

    def test_missing_phase_skip_fake_counts_and_udp_never_pass(self):
        original = copy.deepcopy(self.events)
        self.events.pop()
        self.assertFalse(self.check()['valid_evidence'])
        self.events = original
        self.events[-1]['service_count'] = 1
        self.assertFalse(self.check()['passed'])
        self.events = copy.deepcopy(original)
        self.assertFalse(self.check(self.log().replace('\nOK\n', '\nOK (skipped=2)\n'))['passed'])
        self.assertFalse(self.check(self.log().replace('dsoftbus(test)', 'udp(test)'))['valid_evidence'])

if __name__ == '__main__': unittest.main()
