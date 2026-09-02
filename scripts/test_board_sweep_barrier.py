#!/usr/bin/env python3
"""Host-only regression checks for board_sweep publisher-barrier file handling."""

import contextlib
import importlib.util
import io
import os
import pathlib
import tempfile
import unittest
from unittest import mock


MODULE_PATH = pathlib.Path(__file__).with_name('mdds_e2e') / 'board_sweep.py'
SPEC = importlib.util.spec_from_file_location('board_sweep_under_test', MODULE_PATH)
board_sweep = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(board_sweep)


@unittest.skipUnless(
    hasattr(board_sweep.os, 'O_NOFOLLOW') and hasattr(board_sweep.os, 'O_NONBLOCK'),
    'requires POSIX O_NOFOLLOW and O_NONBLOCK; production fails closed without them')
class PublisherBarrierTest(unittest.TestCase):
    def test_accepts_exact_regular_release_file(self):
        with tempfile.TemporaryDirectory() as td:
            path = pathlib.Path(td) / 'release'
            path.write_bytes(b'MDDS_SWEEP_RELEASE token=run_nonce\n')
            self.assertEqual(
                board_sweep.wait_for_barrier_release(str(path), 'run_nonce', 0),
                (True, 'released'))

    def test_rejects_wrong_release_token(self):
        with tempfile.TemporaryDirectory() as td:
            path = pathlib.Path(td) / 'release'
            path.write_bytes(b'MDDS_SWEEP_RELEASE token=another_nonce\n')
            self.assertEqual(
                board_sweep.wait_for_barrier_release(str(path), 'run_nonce', 0),
                (False, 'token_mismatch'))

    def test_times_out_without_release_file(self):
        with tempfile.TemporaryDirectory() as td:
            path = pathlib.Path(td) / 'release'
            self.assertEqual(
                board_sweep.wait_for_barrier_release(str(path), 'run_nonce', 0),
                (False, 'timeout'))

    def test_rejects_symlink_release_file(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            target = root / 'target'
            release = root / 'release'
            target.write_bytes(b'MDDS_SWEEP_RELEASE token=run_nonce\n')
            try:
                os.symlink(target, release)
            except (NotImplementedError, OSError) as e:
                self.skipTest(f'symlink unavailable: {e}')
            self.assertEqual(
                board_sweep.wait_for_barrier_release(str(release), 'run_nonce', 0),
                (False, 'symlink'))

    @unittest.skipUnless(hasattr(os, 'mkfifo'), 'requires POSIX FIFO support')
    def test_rejects_fifo_without_blocking(self):
        with tempfile.TemporaryDirectory() as td:
            release = pathlib.Path(td) / 'release'
            os.mkfifo(release)
            self.assertEqual(
                board_sweep.wait_for_barrier_release(str(release), 'run_nonce', 0),
                (False, 'not_regular'))


class PublisherBarrierCapabilityTest(unittest.TestCase):
    def test_fails_closed_when_no_follow_is_unavailable(self):
        with tempfile.TemporaryDirectory() as td:
            path = pathlib.Path(td) / 'release'
            path.write_bytes(b'MDDS_SWEEP_RELEASE token=run_nonce\n')
            had_attr = hasattr(board_sweep.os, 'O_NOFOLLOW')
            value = getattr(board_sweep.os, 'O_NOFOLLOW', None)
            if had_attr:
                delattr(board_sweep.os, 'O_NOFOLLOW')
            try:
                self.assertEqual(
                    board_sweep.wait_for_barrier_release(str(path), 'run_nonce', 0),
                    (False, 'o_nofollow_unavailable'))
            finally:
                if had_attr:
                    setattr(board_sweep.os, 'O_NOFOLLOW', value)

    def test_fails_closed_when_non_blocking_open_is_unavailable(self):
        with tempfile.TemporaryDirectory() as td:
            path = pathlib.Path(td) / 'release'
            path.write_bytes(b'MDDS_SWEEP_RELEASE token=run_nonce\n')
            had_follow = hasattr(board_sweep.os, 'O_NOFOLLOW')
            follow = getattr(board_sweep.os, 'O_NOFOLLOW', None)
            had_nonblock = hasattr(board_sweep.os, 'O_NONBLOCK')
            nonblock = getattr(board_sweep.os, 'O_NONBLOCK', None)
            if not had_follow:
                setattr(board_sweep.os, 'O_NOFOLLOW', 0)
            if had_nonblock:
                delattr(board_sweep.os, 'O_NONBLOCK')
            try:
                self.assertEqual(
                    board_sweep.wait_for_barrier_release(str(path), 'run_nonce', 0),
                    (False, 'o_nonblock_unavailable'))
            finally:
                if not had_follow:
                    delattr(board_sweep.os, 'O_NOFOLLOW')
                elif follow is not None:
                    setattr(board_sweep.os, 'O_NOFOLLOW', follow)
                if had_nonblock:
                    setattr(board_sweep.os, 'O_NONBLOCK', nonblock)


class PublisherBarrierParserTest(unittest.TestCase):
    def test_rejects_non_finite_barrier_timeout(self):
        for value in ('nan', 'inf', '-inf', '120.1'):
            argv = [
                'board_sweep.py', '--mode', 'pub', '--wait-match',
                '--barrier-release-file', '/tmp/release',
                '--barrier-token', 'run_nonce', '--barrier-timeout-s', value]
            if value == '-inf':
                # argparse otherwise treats a dash-led infinity literal as an
                # option instead of the value we need to validate.
                argv[-2:] = ['--barrier-timeout-s=-inf']
            with self.subTest(value=value), mock.patch.object(board_sweep.sys, 'argv', argv):
                with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as raised:
                    board_sweep.main()
                self.assertEqual(raised.exception.code, 2)


if __name__ == '__main__':
    unittest.main()
