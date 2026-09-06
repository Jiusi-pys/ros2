#!/usr/bin/env python3
"""Host-only metadata rejection tests; no ROS or board access."""

import copy
import json
import errno
import hashlib
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from broker_local_run import mark_executable, validate_result, inspect_rclpy_overlay


class BrokerLocalMetadataTests(unittest.TestCase):
    def setUp(self):
        self.run = 'bl_test'
        self.lib = '/data/local/tmp/ros2/.mdds-owned-runs/bl_test/lib'
        self.status = {'schema_version': 1, 'run_id': self.run, 'role': 'contexts',
                       'namespace': '/mdds_broker_bl_test', 'returncode': 0,
                       'child_pid': 123, 'child_start': '456'}
        self.record = 'MDDS_OWNED_PROCESS RUN_ID=bl_test TAG=contexts_child PID=123 START=456\n'
        provenance = {'pid': 123, 'libmdds_paths': [self.lib + '/libmdds.so'],
                      'librmw_mdds_paths': [self.lib + '/librmw_mdds.so'], 'owned_udp_sockets': []}
        self.result = {'mode': 'contexts', 'run_id': self.run, 'verdict': 'PASS',
                       'physical_dsoftbus_proven': False, 'before': provenance,
                       'after': copy.deepcopy(provenance), 'initial_samples_per_direction': 5,
                       'alpha_retired': True, 'beta_to_fresh_gamma': True,
                       'beta_received': [f'{self.run}:alpha:{n}' for n in range(5)] +
                       [f'{self.run}:gamma:after_alpha_shutdown'],
                       'gamma_received': [f'{self.run}:beta:after_alpha_shutdown']}

    def log(self, result=None, status=None):
        return ('BROKER_LOCAL_ROS_RESULT ' + json.dumps(result or self.result) + '\n' +
                'GRAPH_PROCESS_EXIT ' + json.dumps(status or self.status) + '\n')

    def errors(self, **kwargs):
        return validate_result(kwargs.get('log', self.log()), kwargs.get('status', self.status),
                               kwargs.get('record', self.record), self.run, 'contexts', self.lib, 5)

    def test_exact_context_metadata_passes(self):
        self.assertEqual([], self.errors())

    def test_printed_pass_cannot_replace_real_nonzero_exit(self):
        status = dict(self.status, returncode=139)
        self.assertTrue(self.errors(status=status, log=self.log(status=status)))

    def test_terminal_identity_and_namespace_must_match(self):
        for key, value in [('run_id', 'stale'), ('namespace', '/old'), ('child_pid', 456),
                           ('child_start', '0'), ('returncode', False)]:
            with self.subTest(key=key):
                status = dict(self.status, **{key: value})
                self.assertTrue(self.errors(status=status, log=self.log(status=status)))

    def test_duplicate_or_missing_result_and_terminal_are_rejected(self):
        for log in ['', self.log() + self.log(), self.log().split('GRAPH_PROCESS_EXIT')[0]]:
            with self.subTest(log=log):
                self.assertTrue(self.errors(log=log))

    def test_wrong_overlay_or_udp_cannot_pass(self):
        for field, value in [('libmdds_paths', ['/shared/libmdds.so']),
                             ('librmw_mdds_paths', ['/shared/librmw_mdds.so']),
                             ('owned_udp_sockets', [{'inode': '123'}]), ('pid', 999)]:
            with self.subTest(field=field):
                result = copy.deepcopy(self.result)
                result['after'][field] = value
                self.assertTrue(self.errors(log=self.log(result=result)))

    def test_payload_retirement_and_physical_scope_are_enforced(self):
        for field, value in [('beta_received', []), ('gamma_received', ['old']),
                             ('alpha_retired', False), ('beta_to_fresh_gamma', False),
                             ('initial_samples_per_direction', 0),
                             ('physical_dsoftbus_proven', True), ('verdict', 'FAIL')]:
            with self.subTest(field=field):
                result = dict(self.result, **{field: value})
                self.assertTrue(self.errors(log=self.log(result=result)))

    def test_daemon_ready_stop_and_actual_pid_are_required(self):
        status = dict(self.status, role='daemon')
        record = self.record.replace('contexts_child', 'daemon_child')
        socket = '/data/local/tmp/ros2/.mdds-owned-runs/bl_test/b.sock'
        ready = ('MDBC_LOCAL_READY mode=experimental-local-only run_id=bl_test '
                 f'domain=49 uid=0 socket={socket} pid=123\n')
        stop = ('MDBC_LOCAL_STOP mode=experimental-local-only run_id=bl_test '
                'result=PASS reason=signal connections=0 active_ports=0\n')
        exit_line = 'GRAPH_PROCESS_EXIT ' + json.dumps(status) + '\n'
        def errors(log):
            return validate_result(log, status, record, self.run, 'daemon', self.lib, 5, socket)
        self.assertEqual([], errors(ready + stop + exit_line))
        for log in [ready + exit_line, ready.replace('pid=123', 'pid=999') + stop + exit_line,
                    ready + stop.replace('result=PASS', 'result=FAIL') + exit_line]:
            self.assertTrue(errors(log))

    def test_executable_helper_rejects_nofollow_link_error_without_chmod(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / 'external'
            target.write_bytes(b'outside')
            before = target.stat().st_mode
            def reject_link(path, flags):
                self.assertTrue(flags & 0x20000)
                raise OSError(errno.ELOOP, 'symlink refused')
            with mock.patch.object(os, 'O_NOFOLLOW', 0x20000, create=True), \
                    mock.patch.object(os, 'open', side_effect=reject_link), \
                    mock.patch.object(os, 'fchmod', create=True) as chmod:
                with self.assertRaises(OSError):
                    mark_executable(str(Path(directory) / 'symlink'), '0' * 64)
                chmod.assert_not_called()
            self.assertEqual(before, target.stat().st_mode)
            self.assertEqual(b'outside', target.read_bytes())

    def test_executable_helper_hashes_and_chmods_the_same_regular_fd(self):
        with tempfile.TemporaryDirectory() as directory:
            artifact = Path(directory) / 'daemon'
            artifact.write_bytes(b'known daemon')
            expected = hashlib.sha256(artifact.read_bytes()).hexdigest()
            original_open = os.open
            opened = []
            def open_regular(path, flags):
                self.assertTrue(flags & 0x20000)
                fd = original_open(path, os.O_RDONLY | getattr(os, 'O_BINARY', 0))
                opened.append(fd)
                return fd
            with mock.patch.object(os, 'O_NOFOLLOW', 0x20000, create=True), \
                    mock.patch.object(os, 'open', side_effect=open_regular), \
                    mock.patch.object(os, 'fchmod', create=True) as chmod:
                self.assertEqual(expected, mark_executable(str(artifact), expected))
                chmod.assert_called_once_with(opened[0], 0o700)
                chmod.reset_mock()
                with self.assertRaises(ValueError):
                    mark_executable(str(artifact), '0' * 64)
                chmod.assert_not_called()



class BrokerRclpyOverlayTests(unittest.TestCase):
    def setUp(self):
        BrokerLocalMetadataTests.setUp(self)
        self.native = '_rclpy_pybind11.cpython-312-aarch64-linux-ohos.so'
        self.python_root = self.lib.rsplit('/', 1)[0] + '/python'
        self.manifest_sha = 'b' * 64
        self.manifest = {'native_name': self.native, 'native_sha256': 'a' * 64,
                         'archive_sha256': 'c' * 64, 'files': {'rclpy/__init__.py': {}, 'rclpy/' + self.native: {}}}
        self.rclpy = {'package_file': self.python_root + '/rclpy/__init__.py',
                      'native_file': self.python_root + '/rclpy/' + self.native,
                      'mapped_native_paths': [self.python_root + '/rclpy/' + self.native],
                      'native_sha256': 'a' * 64, 'manifest_sha256': self.manifest_sha,
                      'package_archive_sha256': 'c' * 64, 'package_files_verified': 2}

    def validate(self, result=None, role='contexts'):
        result = result if result is not None else self.result
        status = dict(self.status, role=role)
        record = self.record.replace('contexts_child', role + '_child')
        log = ('BROKER_LOCAL_ROS_RESULT ' + json.dumps(result) + '\n' +
               'GRAPH_PROCESS_EXIT ' + json.dumps(status) + '\n')
        return validate_result(log, status, record, self.run, role, self.lib, 5,
                               rclpy_manifest=self.manifest, rclpy_manifest_sha256=self.manifest_sha)

    def test_selected_overlay_requires_before_and_after_loaded_native_proof(self):
        self.assertTrue(self.validate())
        self.result['before']['rclpy'] = copy.deepcopy(self.rclpy)
        self.result['after']['rclpy'] = copy.deepcopy(self.rclpy)
        self.assertEqual([], self.validate())
        for key, value in [('package_file', '/shared/rclpy/__init__.py'),
                           ('native_file', '/shared/' + self.native),
                           ('mapped_native_paths', ['/shared/' + self.native]),
                           ('native_sha256', '0' * 64), ('manifest_sha256', '0' * 64),
                           ('package_files_verified', 1)]:
            with self.subTest(key=key):
                result = copy.deepcopy(self.result)
                result['after']['rclpy'][key] = value
                self.assertTrue(self.validate(result))

    def test_worker_overlay_requires_a_second_provenance_after_its_barrier(self):
        provenance = copy.deepcopy(self.result['before'])
        provenance['rclpy'] = copy.deepcopy(self.rclpy)
        result = {'mode': 'worker', 'role': 'alpha', 'run_id': self.run, 'verdict': 'PASS',
                  'physical_dsoftbus_proven': False, 'provenance': provenance,
                  'received': [f'{self.run}:beta:{n}' for n in range(5)],
                  'peer_retired': False, 'completion_barrier': True, 'release_phase': 'received'}
        self.assertTrue(self.validate(result, role='alpha'))
        result['after_provenance'] = copy.deepcopy(provenance)
        self.assertEqual([], self.validate(result, role='alpha'))
        result['after_provenance']['rclpy']['native_sha256'] = '0' * 64
        self.assertTrue(self.validate(result, role='alpha'))

    def test_runtime_probe_rejects_shared_mapping_and_modified_package_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'python'
            package = root / 'rclpy'
            package.mkdir(parents=True)
            initializer = package / '__init__.py'
            initializer.write_bytes(b'private package')
            native = package / self.native
            native.write_bytes(b'private native')
            files = {p.relative_to(root).as_posix(): {'sha256': hashlib.sha256(p.read_bytes()).hexdigest(),
                                                    'bytes': p.stat().st_size} for p in (initializer, native)}
            manifest = {'schema_version': 1, 'native_name': self.native,
                        'native_sha256': files['rclpy/' + self.native]['sha256'],
                        'archive_sha256': 'c' * 64, 'files': files}
            manifest_file = Path(directory) / 'manifest.json'
            manifest_file.write_text(json.dumps(manifest), encoding='utf-8')
            expected = hashlib.sha256(manifest_file.read_bytes()).hexdigest()
            arguments = {'package_file': str(initializer), 'native_file': str(native),
                         'maps_text': '1-2 r-xp 0 00:00 1 ' + str(native) + '\n'}
            record = inspect_rclpy_overlay(root, manifest_file, expected, **arguments)
            self.assertEqual(str(native), record.get('native_file'))
            self.assertEqual(2, record.get('package_files_verified'))
            with self.assertRaises(ValueError):
                inspect_rclpy_overlay(root, manifest_file, '0' * 64, **arguments)
            bad = dict(arguments, maps_text=arguments['maps_text'] +
                       '3-4 r-xp 0 00:00 2 /shared/' + self.native + '\n')
            with self.assertRaises(ValueError):
                inspect_rclpy_overlay(root, manifest_file, expected, **bad)
            initializer.write_bytes(b'changed package')
            with self.assertRaises(ValueError):
                inspect_rclpy_overlay(root, manifest_file, expected, **arguments)


if __name__ == '__main__':
    unittest.main()
