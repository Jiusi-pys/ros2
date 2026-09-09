"""Exercise READY path validation without invoking HDC or a board."""
import hashlib
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
BASH = os.environ.get('ROS2_TEST_BASH', 'C:/Program Files/Git/bin/bash.exe')


def function(name):
    source = (ROOT / 'scripts/run_board_tests.sh').read_text()
    match = re.search(r'^' + re.escape(name) + r'\(\).*?^}', source, re.M | re.S)
    if not match:
        raise AssertionError('missing function ' + name)
    return match.group()


def bash(code, *args, environment=None):
    env = os.environ.copy()
    if environment:
        env.update(environment)
    return subprocess.run([BASH, '-c', code, 'ready-path-test', *map(str, args)],
                          capture_output=True, text=True, timeout=20, env=env)


class ReadyPaths(unittest.TestCase):
    def accepted_host(self, path):
        # Windows command-line parsing can split an unquoted newline argument.
        # Preserve hostile path bytes in an environment value instead.
        return bash(function('safe_relative_path') + '\nsafe_relative_path "$ROS2_TEST_RELATIVE"',
                    environment={'ROS2_TEST_RELATIVE': path}).returncode == 0

    def test_cpp_runtime_names_are_valid_relative_paths(self):
        for path in ['libc++_shared.so', 'runtime/libc++_shared.so.1', 'test/test_case.cpp']:
            with self.subTest(path=path):
                self.assertTrue(self.accepted_host(path))

    def test_path_and_shell_boundaries_stay_rejected(self):
        for path in ['', '../libc++_shared.so', 'lib/../x.so', '/lib/x.so',
                     './libc++_shared.so', 'lib//x.so', 'lib/./x.so', '+leading.so',
                     'with space.so', 'x;touch', 'x$(value)', 'x`value`', 'x\ny.so']:
            with self.subTest(path=path):
                self.assertFalse(self.accepted_host(path))

    def remote_ready(self, relative, *, corrupt=False):
        with tempfile.TemporaryDirectory(prefix='boardtest_ready_paths_') as directory:
            root = Path(directory)
            # The malformed-path cases never create files outside this root.
            runtime = root / 'runtime'; runtime.mkdir()
            payload = b'frozen c++ runtime bytes\n'
            (runtime / 'libc++_shared.so').write_bytes(payload + (b'changed' if corrupt else b''))
            manifest = root / 'manifest'
            manifest.write_text('ROS2_BOARDTEST_MANIFEST V=1 RUN_ID=path_test NONCE=n_one PACKAGE=demo\n'
                                f'SHA256={hashlib.sha256(payload).hexdigest()} PATH={relative}\n',
                                encoding='utf-8', newline='\n')
            posix = bash('cygpath -au "$1"', root).stdout.strip()
            self.assertTrue(posix.startswith('/'))
            code = function('valid_sha256') + '\n' + function('verify_remote_ready_and_create') + r'''
shell() { bash -c "$1"; }
RUN_ID=path_test
RUN_NONCE=n_one
READY_MANIFEST_SHA256="$4"
LOGDIR="$1"
verify_remote_ready_and_create "$1" demo "$2" "$3"
'''
            result = bash(code, posix, posix + '/manifest', posix + '/ready',
                          hashlib.sha256(manifest.read_bytes()).hexdigest())
            return result, (root / 'ready').read_bytes() if (root / 'ready').exists() else None

    def test_generated_remote_validator_hashes_cpp_runtime_before_ready(self):
        result, ready = self.remote_ready('runtime/libc++_shared.so')
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertIsNotNone(ready)
        self.assertTrue(ready.startswith(b'ROS2_BOARDTEST_READY RUN_ID=path_test '))

    def test_remote_validator_still_rejects_changed_bytes_and_traversal(self):
        for relative, corrupt in [('runtime/libc++_shared.so', True),
                                  ('../libc++_shared.so', False),
                                  ('runtime/../runtime/libc++_shared.so', False),
                                  ('/runtime/libc++_shared.so', False)]:
            with self.subTest(relative=relative, corrupt=corrupt):
                result, ready = self.remote_ready(relative, corrupt=corrupt)
                self.assertNotEqual(0, result.returncode)
                self.assertIsNone(ready)


if __name__ == '__main__':
    unittest.main()
