#!/usr/bin/env python3
"""Contract tests for the MDDS CLI/graph phase gate; no board access."""

import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import cli_acceptance as acceptance


class AcceptanceTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.prefix = self.root / 'prefix'
        metadata = self.prefix / 'Lib/site-packages/ros2action-1.0.egg-info'
        metadata.mkdir(parents=True)
        (metadata / 'entry_points.txt').write_text(
            '[ros2cli.command]\naction = ros2action.command.action:ActionCommand\n'
            '[ros2action.verb]\nlist = ros2action.verb.list:ListVerb\n'
            'send_goal = ros2action.verb.send_goal:SendGoalVerb\n', encoding='utf-8')
        self.inventory = acceptance.discover_inventory(self.prefix)
        self.manifest = acceptance.make_manifest(self.inventory)
        self.manifest['run_id'] = 'acceptance_run_1'

    def write_json(self, path, value):
        path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def pass_all(self):
        for case in self.manifest['cases']:
            self.pass_case(case)

    def pass_case(self, case):
        case['status'] = 'PASS'
        log_name = case['id'].replace(':', '_').replace('/', '_') + '.log'
        executions = []
        for index, board in enumerate(case['execution_boards']):
            argv = case['command'] + ['fixture_argument'] if index == 0 and case['command'] else ['fixture']
            terminal = acceptance.terminal_marker(self.manifest['run_id'], case['id'], 0, argv, board)
            log = self.root / (str(index) + '_' + log_name)
            log.write_text('FUNCTIONAL_RESULT exact_fixture\n' + terminal + '\n', encoding='utf-8')
            executions.append({
                'argv': argv, 'returncode': 0, 'board_serial': board,
                'log': {'path': log.name, 'sha256': hashlib.sha256(log.read_bytes()).hexdigest()},
            })
        receipt = {
            'schema_version': 1,
            'run_id': self.manifest['run_id'],
            'case_id': case['id'],
            'kind': 'functional',
            'status': 'PASS',
            'board_serials': self.manifest['target']['board_serials'],
            'rmw_implementation': 'rmw_mdds',
            'transport': 'dsoftbus',
            'executions': executions,
            'assertions': [{
                'id': name, 'passed': True, 'execution': 0,
                'pattern': 'FUNCTIONAL_RESULT exact_fixture',
            } for name in case['assertions']],
        }
        receipt_path = self.root / (log_name + '.json')
        digest = self.write_json(receipt_path, receipt)
        case['evidence'] = [{'path': receipt_path.name, 'sha256': digest}]

    def rewrite_receipt(self, case, mutate):
        ref = case['evidence'][0]
        path = self.root / ref['path']
        receipt = json.loads(path.read_text(encoding='utf-8'))
        mutate(receipt)
        ref['sha256'] = self.write_json(path, receipt)

    def validate(self, phase=1):
        return acceptance.validate_manifest(self.manifest, self.inventory, self.root, phase=phase)

    def test_inventory_extracts_installed_verbs_and_ignores_other_entry_points(self):
        ids = {case['id'] for case in self.manifest['cases']}
        self.assertIn('cli:action/send_goal', ids)
        self.assertIn('cli:action/list', ids)
        self.assertNotIn('cli:action', ids)
        self.assertTrue(all(case['status'] == 'NOT_RUN' for case in self.manifest['cases']))

    def test_extra_command_and_alias_default_operations_are_included(self):
        metadata = self.prefix / 'Lib/site-packages/extra-1.0.dist-info'
        metadata.mkdir()
        (metadata / 'entry_points.txt').write_text(
            '[ros2cli.command]\nwtf = ros2doctor.command.doctor:WtfCommand\n'
            'doctor = ros2doctor.command.doctor:DoctorCommand\n'
            'plugin = ros2plugin.command.plugin:PluginCommand\n'
            '[ros2doctor.verb]\nhello = ros2doctor.verb.hello:HelloVerb\n'
            '[ros2plugin.verb]\nlist = ros2plugin.verb.list:ListVerb\n', encoding='utf-8')
        inventory = acceptance.discover_inventory(self.prefix)
        ids = {case['id'] for case in acceptance.make_manifest(inventory)['cases']}
        self.assertTrue({'cli:doctor', 'cli:doctor/hello', 'cli:wtf', 'cli:wtf/hello',
                         'cli:plugin/list'} <= ids)

    def test_empty_install_and_duplicate_commands_are_rejected(self):
        with self.assertRaises(ValueError):
            acceptance.discover_inventory(self.root / 'missing')
        duplicate = self.prefix / 'Lib/site-packages/duplicate.dist-info'
        duplicate.mkdir()
        (duplicate / 'entry_points.txt').write_text(
            '[ros2cli.command]\naction = other.command:Action\n', encoding='utf-8')
        with self.assertRaises(ValueError):
            acceptance.discover_inventory(self.prefix)

    def test_all_not_run_locks_both_goals(self):
        result = self.validate(phase=2)
        self.assertFalse(result['phase1_pass'])
        self.assertFalse(result['gateway_unlocked'])
        self.assertFalse(result['gate_pass'])

    def test_complete_functional_receipts_unlock_gateway_start(self):
        self.pass_all()
        result = self.validate(phase=2)
        self.assertTrue(result['phase1_pass'], result['errors'])
        self.assertTrue(result['gateway_unlocked'])
        self.assertTrue(result['gate_pass'])
        self.assertEqual(result['gateway_status'], 'NOT_TESTED')

    def test_each_nonpass_and_unknown_status_is_gate_failure(self):
        self.pass_all()
        for status in ('NOT_RUN', 'SKIP', 'FAIL', 'BLOCKED', 'UNKNOWN', 'WAIVED', '', None):
            with self.subTest(status=status):
                self.manifest['cases'][0]['status'] = status
                self.assertFalse(self.validate()['gate_pass'])

    def test_missing_extra_duplicate_and_empty_cases_are_rejected(self):
        self.pass_all()
        cases = copy.deepcopy(self.manifest['cases'])
        for replacement in (cases[1:], cases + [cases[0]], [],
                            cases + [{**cases[0], 'id': 'cli:invented'}]):
            self.manifest['cases'] = replacement
            self.assertFalse(self.validate()['gate_pass'])

    def test_inventory_drift_and_manifest_criterion_changes_fail(self):
        self.pass_all()
        self.manifest['inventory']['commands']['action'] = 'other:Action'
        self.assertFalse(self.validate()['gate_pass'])
        self.manifest['inventory'] = self.inventory
        self.manifest['cases'][0]['assertions'] = []
        self.assertFalse(self.validate()['gate_pass'])

    def test_pass_without_evidence_is_rejected(self):
        self.pass_all()
        self.manifest['cases'][0]['evidence'] = []
        self.assertFalse(self.validate()['gate_pass'])

    def test_wrong_receipt_identity_rmw_transport_board_or_kind_is_rejected(self):
        self.pass_all()
        case = self.manifest['cases'][0]
        changes = {
            'run_id': 'old_run', 'case_id': 'cli:other', 'kind': 'help',
            'rmw_implementation': 'rmw_cyclonedds_cpp', 'transport': 'udp',
            'board_serials': ['only_one_board'], 'status': 'SKIP',
        }
        for key, value in changes.items():
            with self.subTest(key=key):
                self.pass_case(case)
                self.rewrite_receipt(case, lambda receipt: receipt.update({key: value}))
                self.assertFalse(self.validate()['gate_pass'])

    def test_help_or_count_probe_cannot_replace_real_action_send_goal(self):
        self.pass_all()
        case = next(case for case in self.manifest['cases'] if case['id'] == 'cli:action/send_goal')
        for argv in (['ros2', 'action', 'send_goal', '--help'],
                     ['python3.12', 'cli_probe_action.py']):
            with self.subTest(argv=argv):
                self.pass_case(case)
                self.rewrite_receipt(case, lambda receipt: receipt['executions'][0].update(argv=argv))
                self.assertFalse(self.validate()['gate_pass'])

    def test_timeout_missing_terminal_and_failed_assertion_are_rejected(self):
        self.pass_all()
        case = self.manifest['cases'][0]
        for mutation in (
            lambda receipt: receipt['executions'][0].update(returncode=124),
            lambda receipt: receipt['assertions'][0].update(passed=False),
            lambda receipt: receipt['assertions'][0].update(pattern='not in log'),
            lambda receipt: receipt.update(assertions=[]),
            lambda receipt: receipt.update(executions=[]),
        ):
            self.pass_case(case)
            self.rewrite_receipt(case, mutation)
            self.assertFalse(self.validate()['gate_pass'])
        self.pass_case(case)
        def remove_terminal(receipt):
            ref = receipt['executions'][0]['log']
            log = self.root / ref['path']
            log.write_text('FUNCTIONAL_RESULT exact_fixture\n', encoding='utf-8')
            ref['sha256'] = hashlib.sha256(log.read_bytes()).hexdigest()
        self.rewrite_receipt(case, remove_terminal)
        self.assertFalse(self.validate()['gate_pass'])

    def test_receipt_hash_raw_log_hash_and_path_escape_are_rejected(self):
        self.pass_all()
        case = self.manifest['cases'][0]
        case['evidence'][0]['sha256'] = '0' * 64
        self.assertFalse(self.validate()['gate_pass'])
        self.pass_case(case)
        self.rewrite_receipt(case, lambda receipt: receipt['executions'][0]['log'].update(sha256='0' * 64))
        self.assertFalse(self.validate()['gate_pass'])
        self.pass_case(case)
        case['evidence'][0]['path'] = '../outside.json'
        self.assertFalse(self.validate()['gate_pass'])

    def test_malformed_manifest_fails_without_traceback(self):
        for manifest in ({}, {'schema_version': 999}, [], {'cases': None}):
            with self.subTest(manifest=manifest):
                result = acceptance.validate_manifest(manifest, self.inventory, self.root)
                self.assertFalse(result['gate_pass'])

    def test_one_board_receipt_cannot_claim_cross_board_graph_or_transport(self):
        self.pass_all()
        case = next(case for case in self.manifest['cases'] if case['id'] == 'graph:remote_multi_node')
        self.rewrite_receipt(case, lambda receipt: receipt.update(executions=receipt['executions'][:1]))
        self.assertFalse(self.validate()['gate_pass'])

    def test_malformed_nested_execution_and_assertion_fail_closed(self):
        self.pass_all()
        case = self.manifest['cases'][0]
        for mutation in (
            lambda receipt: receipt.update(executions=[None]),
            lambda receipt: receipt.update(assertions=[None]),
            lambda receipt: receipt['assertions'][0].update(id=[]),
        ):
            self.pass_case(case)
            self.rewrite_receipt(case, mutation)
            self.assertFalse(self.validate()['gate_pass'])

    def test_cli_returns_nonzero_for_unfinished_goal_and_writes_report(self):
        manifest_path = self.root / 'manifest.json'
        self.write_json(manifest_path, self.manifest)
        report_path = self.root / 'report.json'
        process = subprocess.run(
            [sys.executable, str(Path(acceptance.__file__)), 'verify',
             '--manifest', str(manifest_path), '--prefix', str(self.prefix),
             '--evidence-root', str(self.root), '--phase', '2', '--report', str(report_path)],
            capture_output=True, text=True, check=False)
        self.assertEqual(process.returncode, 1, process.stderr)
        self.assertFalse(json.loads(report_path.read_text())['gateway_unlocked'])
        self.assertNotIn('Traceback', process.stderr)


if __name__ == '__main__':
    unittest.main()
