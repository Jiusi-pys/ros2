"""Executable source discovery boundary contracts for the standalone workspace."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).with_name("manifest_source_roots.py")


class ManifestRootsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "source with spaces"
        self.source.mkdir()
        self.manifest = self.root / "input.repos"

    def run_roots(self, keys=None, raw=None):
        if raw is None:
            raw = json.dumps({"repositories": {key: {"type": "git", "url": "https://example.invalid/repo", "version": "stand"} for key in keys}})
        self.manifest.write_text(raw, encoding="utf-8")
        return subprocess.run([sys.executable, str(SCRIPT), "--manifest", str(self.manifest), "--source-root", str(self.source)], text=True, capture_output=True)

    def test_existing_roots_sorted_deduplicated_and_unlisted_siblings_excluded(self):
        for name in ("z", "a", "unlisted"):
            (self.source / name).mkdir()
        result = self.run_roots(["z", "a", "a/."])
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual([str((self.source / name).resolve()) for name in ("a", "z")], result.stdout.splitlines())

    def test_partial_import_warns_but_succeeds(self):
        (self.source / "present").mkdir()
        result = self.run_roots(["missing", "present"])
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertIn("missing", result.stderr)
        self.assertEqual([str((self.source / "present").resolve())], result.stdout.splitlines())

    def test_no_existing_root_fails_without_broad_fallback(self):
        result = self.run_roots(["missing"])
        self.assertNotEqual(0, result.returncode)
        self.assertEqual("", result.stdout)

    def test_malformed_yaml_and_schema_rejected(self):
        for raw in ("repositories: [", "[]", "{}", "repositories: []", "repositories: {}"):
            with self.subTest(raw=raw):
                result = self.run_roots(raw=raw)
                self.assertNotEqual(0, result.returncode)
                self.assertEqual("", result.stdout)

    def test_unsafe_keys_rejected_without_partial_stdout(self):
        (self.source / "valid").mkdir()
        for name in ("../outside", ".", "", str(self.root), "C:/outside", "..\\outside"):
            with self.subTest(name=name):
                result = self.run_roots(["valid", name])
                self.assertNotEqual(0, result.returncode)
                self.assertEqual("", result.stdout)

    def test_files_are_not_source_directories(self):
        (self.source / "file").write_text("not a directory", encoding="utf-8")
        result = self.run_roots(["file"])
        self.assertNotEqual(0, result.returncode)
        self.assertEqual("", result.stdout)

    def test_symlink_or_windows_junction_escape_rejected(self):
        outside = self.root / "outside"
        outside.mkdir()
        link = self.source / "escape"
        if os.name == "nt":
            # Directory junctions need no Windows Developer Mode privileges.
            command = "$null = New-Item -ItemType Junction -Path $env:TEST_LINK -Target $env:TEST_TARGET"
            subprocess.run(["powershell", "-NoProfile", "-Command", command], env=dict(os.environ, TEST_LINK=str(link), TEST_TARGET=str(outside)), check=True, capture_output=True)
        else:
            link.symlink_to(outside, target_is_directory=True)
        self.assertTrue(link.is_dir(), "escape fixture was not created")
        self.addCleanup(lambda: os.rmdir(link) if os.name == "nt" else link.unlink())
        result = self.run_roots(["escape"])
        self.assertNotEqual(0, result.returncode)
        self.assertEqual("", result.stdout)


if __name__ == "__main__":
    unittest.main()
