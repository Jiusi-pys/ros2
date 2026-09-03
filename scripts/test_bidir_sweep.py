#!/usr/bin/env python3
"""Pure parser regressions for the GW-10 endpoint harness."""

import importlib.util
import pathlib
import unittest


MODULE_PATH = pathlib.Path(__file__).with_name('mdds_e2e') / 'bidir_sweep.py'
SPEC = importlib.util.spec_from_file_location('bidir_sweep', MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


def common_args(role):
    return [
        '--role', role,
        '--direction-out', 'pc_to_b' if role == 'pc' else 'b_to_pc',
        '--direction-in', 'b_to_pc' if role == 'pc' else 'pc_to_b',
        '--pub-topic', '/gw10/out',
        '--sub-topic', '/gw10/in',
        '--count', '4096', '--size', '1024', '--rate-hz', '20',
        '--overall-timeout-s', '360',
    ]


class BidirSweepParserTest(unittest.TestCase):

    def test_pc_without_barrier_keeps_absent_state(self):
        args = MODULE.parse_args(common_args('pc'))
        self.assertIsNone(args.barrier_release_file)
        self.assertEqual(args.barrier_token, '')
        self.assertEqual(args.barrier_timeout_s, 0.0)

    def test_board_barrier_accepts_safe_absolute_path(self):
        args = MODULE.parse_args(common_args('board_b') + [
            '--barrier-release-file', '/data/local/tmp/ros2/gw10.release',
            '--barrier-token', 'gw10_nonce', '--barrier-timeout-s', '120',
        ])
        self.assertEqual(args.barrier_release_file,
                         '/data/local/tmp/ros2/gw10.release')

    def test_pc_rejects_barrier_arguments(self):
        with self.assertRaises(SystemExit):
            MODULE.parse_args(common_args('pc') + [
                '--barrier-release-file', '/data/local/tmp/ros2/gw10.release',
                '--barrier-token', 'gw10_nonce', '--barrier-timeout-s', '120',
            ])


if __name__ == '__main__':
    unittest.main()
