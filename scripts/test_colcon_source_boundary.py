"""Execute the build script's colcon commands with an argument-recording stub."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


class ColconSourceBoundaryTests(unittest.TestCase):
    def test_manifest_selection_and_invalid_override_are_fail_closed(self):
        source = Path(__file__).with_name("build_ohos.sh").read_text()
        start = source.index('if [ "$OHOS_REQUIRE_CLEAN"', source.index('# Resolve once'))
        end = source.index('\n# Verify the exact', start)
        block = source[start:end]
        stub = 'pixi() { local last="" arg; for arg in "$@"; do if [ "$last" = --manifest ]; then case "$arg" in */invalid) return 1 ;; esac; printf "%s\\n" "$arg"; return 0; fi; last="$arg"; done; }\n'
        for clean, override, expected in [('0', '', 'ros2.repos'), ('1', '', 'ros2.ohos.lock.repos'), ('1', '/fixture/custom', 'custom'), ('0', '/fixture/invalid', None)]:
            with self.subTest(clean=clean, override=override):
                script = 'set -e\nWORKSPACE_ROOT=/fixture\n' + stub + block + '\nprintf "%s" "${SOURCE_ROOTS[0]}"'
                result = subprocess.run(['C:/Program Files/Git/bin/bash.exe'], input=script,
                    env=dict(os.environ, OHOS_REQUIRE_CLEAN=clean, OHOS_SOURCE_MANIFEST=override), capture_output=True, text=True)
                if expected is None:
                    self.assertNotEqual(0, result.returncode)
                    self.assertEqual('', result.stdout)
                else:
                    self.assertEqual(0, result.returncode, result.stderr)
                    self.assertEqual('/fixture/' + expected, result.stdout)

    def test_relative_replay_manifest_survives_destination_change(self):
        source = Path(__file__).with_name("verify_fresh_lock_replay.sh").read_text()
        assignments = "\n".join(line for line in source.splitlines() if line.startswith('LOCK='))
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'custom.repos').write_text('repositories: {}')
            (root / 'destination').mkdir()
            script = 'set -e\ncd "$TEST_ROOT"\nROOT="$PWD"\nOHOS_SOURCE_MANIFEST=custom.repos\n' + assignments + '\ncd destination\ncat "$LOCK"'
            result = subprocess.run(['C:/Program Files/Git/bin/bash.exe', '-c', script],
                                    env=dict(os.environ, TEST_ROOT=root.as_posix()), capture_output=True, text=True)
            self.assertEqual(0, result.returncode, result.stderr)
            self.assertEqual('repositories: {}', result.stdout)
        self.assertIn('OHOS_SOURCE_MANIFEST="$LOCK" bash scripts/apply_patches.sh', source)
        self.assertIn('import --retry 3 --input "$LOCK"', source)

    def test_list_and_build_receive_identical_explicit_roots(self):
        source = Path(__file__).with_name("build_ohos.sh").read_text(encoding="utf-8")
        # Collapse Bash line continuations, then execute the actual invocation.
        logical = source.replace("\\\n", " ")
        commands = [line for line in logical.splitlines()
                    if re.match(r"\s*pixi run colcon(?: |$)", line)]
        self.assertEqual(2, len(commands), commands)
        script = r'''
set -e
SOURCE_ROOTS=("/fixture/source with spaces/a" "/fixture/source/z")
PACKAGES_SKIP=()
LISTED_PACKAGES=/dev/null
pixi() {
  local previous='' arg
  for arg in "$@"; do
    if [ "$previous" = --base-paths ]; then
      printf '%s\n' "$arg" >> "$CAPTURE"
    elif [ "$arg" = /fixture/source/z ]; then
      printf '%s\n' "$arg" >> "$CAPTURE"
    fi
    previous="$arg"
  done
  printf '\n' >> "$CAPTURE"
}
'''
        with tempfile.TemporaryDirectory() as folder:
            capture = Path(folder) / "arguments.txt"
            result = subprocess.run(
                ["C:/Program Files/Git/bin/bash.exe", "-c", script + "\n".join(commands)],
                env=dict(os.environ, CAPTURE=capture.as_posix()),
                capture_output=True, text=True)
            self.assertEqual(0, result.returncode, result.stderr)
            self.assertEqual(
                ["/fixture/source with spaces/a", "/fixture/source/z", "",
                 "/fixture/source with spaces/a", "/fixture/source/z", ""],
                capture.read_text().splitlines())


if __name__ == "__main__":
    unittest.main()
