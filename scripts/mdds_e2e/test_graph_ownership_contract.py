#!/usr/bin/env python3
"""Host-only regression tests for graph evidence and its process supervisor."""

import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest

spec = importlib.util.spec_from_file_location('graph_fixture', Path(__file__).with_name('board_graph_ownership.py'))
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)


class GraphEvidenceTest(unittest.TestCase):
    def setUp(self):
        self.run_id = 'graph_test'
        self.namespace = '/graph_test'
        self.overlay = '/data/local/tmp/ros2/.mdds-owned-runs/graph_test/lib'
        self.status = {'schema_version': 1, 'run_id': self.run_id, 'role': 'observer',
                       'namespace': self.namespace, 'returncode': 0, 'child_pid': 12, 'child_start': '34'}
        self.log = (
            '[INFO] [rmw_mdds]: mdds transports active: dsoftbus(state=1 peers=1)\n'
            'GRAPH_RMW=rmw_mdds\n'
            'GRAPH_LOADED_LIBS ' + json.dumps({name: self.overlay + '/' + name for name in fixture.LIBRARIES}) + '\n'
            'GRAPH_OWNERSHIP_RESULT PASS\n')

    def validate(self, log=None, status=None):
        return fixture.validate_result(self.log if log is None else log,
                                       self.status if status is None else status,
                                       self.run_id, 'observer', self.namespace, self.overlay)

    def test_complete_ownership_result_and_process_exit_pass(self):
        self.assertEqual(self.validate(), [])

    def test_pass_printed_before_crash_or_timeout_is_failure(self):
        for code in (139, -11, 124, 143, None, '0', False):
            with self.subTest(code=code):
                self.assertTrue(self.validate(status={**self.status, 'returncode': code}))

    def test_mixed_udp_backend_or_no_backend_is_failure(self):
        for transport in ('dsoftbus(state=1) udp(state=1)', 'udp(state=1)', '', 'dsoftbus_fake(state=1)'):
            log = self.log.replace('dsoftbus(state=1 peers=1)', transport)
            self.assertTrue(self.validate(log=log))

    def test_libraries_must_be_loaded_from_this_runs_overlay(self):
        self.assertTrue(self.validate(log=self.log.replace(self.overlay, '/data/local/tmp/ros2/lib')))
        self.assertTrue(self.validate(log='\n'.join(line for line in self.log.splitlines()
                                                   if not line.startswith('GRAPH_LOADED_LIBS '))))

    def test_duplicate_missing_or_conflicting_success_marker_fails(self):
        for log in (self.log + 'GRAPH_OWNERSHIP_RESULT PASS\n',
                    self.log.replace('GRAPH_OWNERSHIP_RESULT PASS', ''),
                    self.log + 'GRAPH_OWNERSHIP_RESULT FAIL\n'):
            self.assertTrue(self.validate(log=log))

    def test_status_binds_run_role_namespace_and_child_identity(self):
        for key, value in {'run_id': 'old', 'role': 'source', 'namespace': '/old',
                           'child_pid': 0, 'child_start': '', 'schema_version': 2}.items():
            self.assertTrue(self.validate(status={**self.status, key: value}))

    def test_missing_or_malformed_status_fails_closed(self):
        for status in ({}, [], None, {'schema_version': 1}):
            self.assertTrue(fixture.validate_result(self.log, status, self.run_id,
                                                    'observer', self.namespace, self.overlay))

    def test_source_requires_readiness_and_clean_exit(self):
        status = {**self.status, 'role': 'source'}
        self.assertTrue(fixture.validate_result(self.log, status, self.run_id,
                                                'source', self.namespace, self.overlay))
        self.assertEqual(fixture.validate_result(self.log + 'GRAPH_SOURCE_READY\n', status,
                                                  self.run_id, 'source', self.namespace, self.overlay), [])

    def test_waited_process_status_preserves_real_exit_codes(self):
        for code in (0, 7):
            with self.subTest(code=code), tempfile.TemporaryDirectory() as directory:
                status_path = Path(directory) / 'status.json'
                observed = fixture.supervise_command(
                    [sys.executable, '-c', f'raise SystemExit({code})'], status_path,
                    self.run_id, 'observer', self.namespace)
                self.assertEqual(observed, code)
                recorded = json.loads(status_path.read_text())
                self.assertEqual(recorded['returncode'], code)
                self.assertEqual(recorded['run_id'], self.run_id)
                self.assertGreater(recorded['child_pid'], 0)

    def test_existing_terminal_record_is_never_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            status_path = Path(directory) / 'status.json'
            status_path.write_text('original')
            with self.assertRaises(FileExistsError):
                fixture.supervise_command([sys.executable, '-c', 'pass'], status_path,
                                          self.run_id, 'observer', self.namespace)
            self.assertEqual(status_path.read_text(), 'original')


class NodeCardinalityTest(unittest.TestCase):
    def check(self, rows):
        observer = SimpleNamespace(get_node_names_and_namespaces=lambda: rows)
        return fixture.inspect_node_cardinality(observer, '/isolated')

    def test_two_duplicate_registrations_are_required(self):
        rows = [('duplicate', '/isolated')] * 2
        self.assertEqual([], self.check(rows))
        for count in (0, 1, 3):
            with self.subTest(count=count):
                self.assertTrue(self.check([('duplicate', '/isolated')] * count))

    def test_other_namespaces_and_observer_do_not_satisfy_duplicate_count(self):
        self.assertTrue(self.check([('duplicate', '/elsewhere')] * 2))
        self.assertTrue(self.check([('observer', '/isolated')] * 2))
        self.assertEqual([], self.check([('duplicate', '/isolated')] * 2 +
                                       [('duplicate', '/elsewhere')] * 2))


if __name__ == '__main__':
    unittest.main()
