#!/usr/bin/env python3
"""Exercise the actual CMake resolver, including fail-closed inputs."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CMAKE = os.environ.get("CMAKE_COMMAND") or shutil.which("cmake")


class VendorLockTest(unittest.TestCase):
    def resolve(self, source):
        with tempfile.TemporaryDirectory(prefix="ohos-vendor-lock-") as directory:
            script = Path(directory) / "test.cmake"
            script.write_text(
                f'include("{ROOT.as_posix()}/cmake/ohos-vendor-lock.cmake")\n'
                f'ohos_resolve_vendor_source("{source["type"]}" "{source["url"]}" '
                f'"{source["requested_version"]}" revision digest)\n'
                'message(STATUS "LOCK_RESULT=${revision}|${digest}")\n',
                encoding="utf-8",
            )
            return subprocess.run([CMAKE, "-P", str(script)], text=True,
                                  capture_output=True, check=False)

    def test_all_locked_sources(self):
        lock = json.loads((ROOT / "cmake/ohos-vendor-sources.lock.json").read_text())
        identities = set()
        for entry in lock["sources"]:
            identity = (entry["type"], entry["url"], entry["requested_version"])
            self.assertNotIn(identity, identities)
            identities.add(identity)
            with self.subTest(source=entry["url"]):
                result = self.resolve(entry)
                self.assertEqual(result.returncode, 0, result.stderr)
                expected = entry.get("commit", entry["requested_version"])
                digest = "SHA256=" + entry["sha256"] if "sha256" in entry else ""
                self.assertIn(f"LOCK_RESULT={expected}|{digest}", result.stdout)

    def test_unknown_tag_url_and_short_hash_rejected(self):
        for url, revision in [("https://github.com/jbeder/yaml-cpp.git", "main"),
                              ("https://example.invalid/yaml-cpp.git", "0.8.0"),
                              ("https://example.invalid/repo.git", "abcd1234")]:
            result = self.resolve(dict(type="git", url=url, requested_version=revision))
            self.assertNotEqual(result.returncode, 0)

    def test_fixed_commit_and_local_source(self):
        for vcs_type, version in [("git", "a" * 40), ("path", "''")]:
            result = self.resolve(dict(type=vcs_type, url="fixture", requested_version=version))
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    if not CMAKE:
        raise SystemExit("cmake is required")
    unittest.main()
