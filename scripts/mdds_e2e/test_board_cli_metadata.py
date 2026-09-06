#!/usr/bin/env python3
"""Host-only metadata CLI oracles and receipt regression tests."""

import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import cli_acceptance as acceptance
import board_cli_metadata as metadata


class MetadataOracleTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.prefix = self.root / 'prefix'
        self.prefix.mkdir()
        self.context = {
            'prefix': self.prefix.as_posix(),
            'packages': ['action_tutorials_interfaces', 'demo_nodes_cpp', 'demo_nodes_py',
                         'example_interfaces', 'rmw_mdds', 'rosbag2_storage_sqlite3', 'std_msgs'],
            'package_xml': '<package format="3"><name>rmw_mdds</name><version>0.1.0</version>'
                           '<member_of_group>rmw_implementation_packages</member_of_group></package>',
            'interfaces': {'Messages': ['std_msgs/msg/String'],
                           'Services': ['example_interfaces/srv/AddTwoInts'],
                           'Actions': ['action_tutorials_interfaces/action/Fibonacci']},
            'package_interfaces': ['example_interfaces/msg/String',
                                   'example_interfaces/srv/AddTwoInts',
                                   'example_interfaces/action/Fibonacci'],
            'interface_packages': ['action_tutorials_interfaces', 'example_interfaces', 'std_msgs'],
            'executables': {},
            'extension_points': ['ros2cli.command', 'ros2topic.verb'],
            'extensions': {'ros2cli.command': ['topic'], 'ros2topic.verb': ['echo', 'list']},
            'plugin_package': 'rosbag2_storage_sqlite3',
            'plugin_lines': ['sqlite3 [rosbag2_storage_plugins::SqliteStorage] '
                             '(base: rosbag2_storage::storage_interfaces::ReadWriteInterface)'],
            'input_hashes': [],
        }
        for package in self.context['packages']:
            marker = self.prefix / 'share/ament_index/resource_index/packages' / package
            marker.parent.mkdir(parents=True, exist_ok=True)
            marker.touch()
        for package, executable in [('demo_nodes_cpp', 'talker'), ('demo_nodes_py', 'talker-script.py')]:
            path = self.prefix / 'lib' / package / executable
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'fixture executable')
            self.context['executables'][package] = [path.as_posix()]

    def outputs(self, case_id):
        c = self.context
        values = {
            'cli:pkg/list': ['\n'.join(c['packages']) + '\n'],
            'cli:pkg/prefix': [c['prefix'] + '\n'],
            'cli:pkg/xml': [c['package_xml'] + '\n'],
            'cli:pkg/executables': ['\n'.join(c['executables'][p]) + '\n'
                                    for p in ('demo_nodes_cpp', 'demo_nodes_py')],
            'cli:interface/list': [''.join(section + ':\n' + ''.join('    ' + name + '\n'
                                      for name in c['interfaces'][section])
                                      for section in ('Messages', 'Services', 'Actions'))],
            'cli:interface/package': ['\n'.join(c['package_interfaces']) + '\n'],
            'cli:interface/packages': ['\n'.join(c['interface_packages']) + '\n'],
            'cli:interface/show': ['string data\n', 'int64 a\nint64 b\n---\nint64 sum\n',
                                   'int32 order\n---\nint32[] sequence\n---\nint32[] partial_sequence\n'],
            'cli:interface/proto': ["data: ''\n"],
            'cli:extension_points': [''.join(name + ': Fixture documentation\n'
                                            for name in c['extension_points'])],
            'cli:extensions': [''.join(group + '\n' + ''.join('  ' + name + ': Fixture\n'
                                         for name in names) for group, names in c['extensions'].items())],
            'cli:plugin/list': [c['plugin_package'] + ':\n' + ''.join('\t' + line + '\n'
                                                                     for line in c['plugin_lines'])],
        }
        return [{'argv': command, 'returncode': 0, 'stdout': stdout, 'stderr': ''}
                for command, stdout in zip(metadata.CASE_COMMANDS[case_id], values[case_id])]

    def test_each_of_twelve_oracles_accepts_complete_functional_output(self):
        self.assertEqual(12, len(metadata.CASE_COMMANDS))
        for case_id in metadata.CASE_COMMANDS:
            with self.subTest(case_id=case_id):
                self.assertEqual([], metadata.check_case(case_id, self.outputs(case_id), self.context))

    def test_each_oracle_rejects_help_missing_output_and_nonzero_real_exit(self):
        for case_id in metadata.CASE_COMMANDS:
            for fault in ('help', 'empty', 'nonzero'):
                with self.subTest(case_id=case_id, fault=fault):
                    outputs = self.outputs(case_id)
                    if fault == 'help':
                        outputs[0]['stdout'] = 'usage: ros2 --help\n'
                    elif fault == 'empty':
                        outputs[0]['stdout'] = ''
                    else:
                        outputs[0]['returncode'] = 7
                    self.assertTrue(metadata.check_case(case_id, outputs, self.context))

    def test_show_requires_all_three_kinds_and_exact_sections(self):
        outputs = self.outputs('cli:interface/show')
        self.assertTrue(metadata.check_case('cli:interface/show', outputs[:1], self.context))
        outputs[1]['stdout'] = 'int64 a\n---\nint64 b\nint64 sum\n'
        self.assertTrue(metadata.check_case('cli:interface/show', outputs, self.context))
        outputs = self.outputs('cli:interface/show')
        outputs[2]['stdout'] = outputs[2]['stdout'].replace('int32[] partial_sequence', 'int32 partial_sequence')
        self.assertTrue(metadata.check_case('cli:interface/show', outputs, self.context))

    def test_interface_list_rejects_correct_name_in_wrong_section(self):
        outputs = self.outputs('cli:interface/list')
        outputs[0]['stdout'] = outputs[0]['stdout'].replace('Services:', 'WrongServices:')
        self.assertTrue(metadata.check_case('cli:interface/list', outputs, self.context))

    def test_inventory_oracles_reject_duplicates_missing_and_extra_entries(self):
        for case_id in ('cli:pkg/list', 'cli:interface/package', 'cli:interface/packages'):
            for mode in ('duplicate', 'missing', 'extra'):
                outputs = self.outputs(case_id)
                lines = outputs[0]['stdout'].splitlines()
                lines = lines + [lines[0]] if mode == 'duplicate' else lines[1:] if mode == 'missing' else lines + ['invented']
                outputs[0]['stdout'] = '\n'.join(lines) + '\n'
                self.assertTrue(metadata.check_case(case_id, outputs, self.context))

    def test_prefix_xml_and_executables_check_the_installed_artifacts(self):
        outputs = self.outputs('cli:pkg/prefix')
        outputs[0]['stdout'] = '/wrong/prefix\n'
        self.assertTrue(metadata.check_case('cli:pkg/prefix', outputs, self.context))
        outputs = self.outputs('cli:pkg/xml')
        outputs[0]['stdout'] = outputs[0]['stdout'].replace('rmw_implementation_packages', 'unrelated')
        self.assertTrue(metadata.check_case('cli:pkg/xml', outputs, self.context))
        outputs = self.outputs('cli:pkg/executables')
        Path(self.context['executables']['demo_nodes_py'][0]).unlink()
        self.assertTrue(metadata.check_case('cli:pkg/executables', outputs, self.context))

    def test_proto_is_parsed_as_yaml_and_checks_value_and_type(self):
        for value in ('data: 0\n', 'wrong: ""\n', 'data: []\n', 'data: "changed"\n', 'data: [\n'):
            outputs = self.outputs('cli:interface/proto')
            outputs[0]['stdout'] = value
            self.assertTrue(metadata.check_case('cli:interface/proto', outputs, self.context))

    def test_extension_load_failures_and_renamed_plugin_implementations_fail(self):
        outputs = self.outputs('cli:extension_points')
        outputs[0]['stdout'] = '- ' + outputs[0]['stdout']
        self.assertTrue(metadata.check_case('cli:extension_points', outputs, self.context))
        outputs = self.outputs('cli:extensions')
        outputs[0]['stdout'] = outputs[0]['stdout'].replace('  echo:', '- echo:')
        self.assertTrue(metadata.check_case('cli:extensions', outputs, self.context))
        outputs = self.outputs('cli:plugin/list')
        outputs[0]['stdout'] = outputs[0]['stdout'].replace('SqliteStorage', 'WrongStorage')
        self.assertTrue(metadata.check_case('cli:plugin/list', outputs, self.context))

    def test_wrong_actual_argv_cannot_satisfy_a_case(self):
        outputs = self.outputs('cli:pkg/list')
        outputs[0]['argv'] = ['ros2', 'pkg', 'list', '--help']
        self.assertTrue(metadata.check_case('cli:pkg/list', outputs, self.context))

    def test_duplicate_demo_console_scripts_do_not_hide_registered_cli_extensions(self):
        first = ('[console_scripts]\nlistener = demo_cpp:listener\n'
                 '[ros2cli.extension_point]\nros2cli.command = ros2cli.command:CommandExtension\n'
                 '[ros2cli.command]\npkg = ros2pkg.command.pkg:PkgCommand\n')
        second = '[console_scripts]\nlistener = demo_py:listener\n'
        points, groups = metadata.extension_registrations([first, second])
        self.assertEqual(['ros2cli.command'], points)
        self.assertEqual({'ros2cli.command': ['pkg']}, groups)
        with self.assertRaises(ValueError):
            metadata.extension_registrations([first, '[ros2cli.command]\npkg = other:Pkg\n'])

    def test_real_subprocess_exit_and_timeout_are_recorded(self):
        result = metadata.execute([sys.executable, '-c', 'print("value"); raise SystemExit(9)'], {}, 5)
        self.assertEqual(9, result['returncode'])
        self.assertIn('value', result['stdout'])
        timeout = metadata.execute([sys.executable, '-c', 'import time; time.sleep(3)'], {}, 0.05)
        self.assertTrue(timeout['timed_out'])
        self.assertNotEqual(0, timeout['returncode'])

    def test_output_directory_requires_exact_run_owner_and_never_reuses_files(self):
        run = self.prefix / '.mdds-owned-runs' / 'metadata_test'
        run.mkdir(parents=True)
        output = run / 'cli_metadata'
        with self.assertRaises(ValueError):
            metadata.claim_output(self.prefix, 'metadata_test', output)
        (run / 'owner').write_text('MDDS_RUN_OWNER RUN_ID=metadata_test LABEL=cli_metadata\n')
        self.assertEqual(output, metadata.claim_output(self.prefix, 'metadata_test', output))
        with self.assertRaises((ValueError, FileExistsError)):
            metadata.claim_output(self.prefix, 'metadata_test', output)
        with self.assertRaises(ValueError):
            metadata.claim_output(self.prefix, 'metadata_test', self.root / 'escape')

    def test_receipts_pass_existing_validator_but_unrun_goal_stays_locked(self):
        template = json.loads(Path(__file__).with_name('cli_acceptance_manifest.json').read_text())
        output = self.root / 'results'
        output.mkdir()
        def run_command(argv, env, timeout):
            for case_id, commands in metadata.CASE_COMMANDS.items():
                for item in self.outputs(case_id):
                    if item['argv'] == argv:
                        return {**item, 'timed_out': False, 'actual_argv': argv}
            self.fail(argv)
        partial = metadata.run_cases(template, self.context, output, 'metadata_test',
                                     acceptance.TARGET['board_serials'][0], {}, executor=run_command)
        passed = [case for case in partial['cases'] if case['status'] == 'PASS']
        self.assertEqual(12, len(passed))
        for case in passed:
            acceptance.validate_receipt(case, case['evidence'][0], partial, output)
        result = acceptance.validate_manifest(partial, partial['inventory'], output, phase=2)
        self.assertFalse(result['phase1_pass'])
        self.assertFalse(result['gateway_unlocked'])
        self.assertEqual(12, result['passed_cases'])
        self.assertTrue(all(case['status'] == 'NOT_RUN' for case in partial['cases']
                            if case['id'] not in metadata.CASE_COMMANDS))


if __name__ == '__main__':
    unittest.main()
