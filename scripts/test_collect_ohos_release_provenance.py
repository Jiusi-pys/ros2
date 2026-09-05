#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import json
import subprocess
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("collect_ohos_release_provenance.py")
SPEC = importlib.util.spec_from_file_location("ohos_provenance", SCRIPT)
assert SPEC and SPEC.loader
provenance = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(provenance)


class ProvenanceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="ohos-provenance-test-")
        self.root = Path(self.temporary.name)
        self.repo = self.root / "repo"
        subprocess.run(["git", "init", "--quiet", str(self.repo)], check=True)
        subprocess.run(["git", "-C", str(self.repo), "config", "user.name", "test"], check=True)
        subprocess.run(
            ["git", "-C", str(self.repo), "config", "user.email", "test@example.invalid"],
            check=True,
        )
        (self.repo / "tracked.txt").write_text("one\n", encoding="utf-8")
        subprocess.run(["git", "-C", str(self.repo), "add", "tracked.txt"], check=True)
        subprocess.run(["git", "-C", str(self.repo), "commit", "--quiet", "-m", "base"], check=True)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_repository_state_binds_tracked_and_untracked_bytes(self) -> None:
        clean = provenance.repository_state(self.repo, "fixture")
        (self.repo / "tracked.txt").write_text("two\n", encoding="utf-8")
        tracked = provenance.repository_state(self.repo, "fixture")
        self.assertNotEqual(clean["state_sha256"], tracked["state_sha256"])
        (self.repo / "untracked.bin").write_bytes(b"\x00fixture\xff")
        untracked = provenance.repository_state(self.repo, "fixture")
        self.assertNotEqual(tracked["state_sha256"], untracked["state_sha256"])
        self.assertEqual(untracked["untracked"][0]["path"], "untracked.bin")

    def test_tree_digest_is_order_independent_and_byte_sensitive(self) -> None:
        tree = self.root / "tree"
        (tree / "b").mkdir(parents=True)
        (tree / "b" / "two").write_text("2\n", encoding="utf-8")
        (tree / "one").write_text("1\n", encoding="utf-8")
        before = provenance.tree_digest(tree)
        self.assertEqual(before["files"], 2)
        (tree / "b" / "two").write_text("changed\n", encoding="utf-8")
        after = provenance.tree_digest(tree)
        self.assertNotEqual(before["sha256"], after["sha256"])

    def test_lock_names_and_atomic_write(self) -> None:
        lock = self.root / "lock.repos"
        lock.write_text(
            "repositories:\n  org/repo:\n    type: git\n    url: https://example.invalid/repo.git\n    version: 0123456789012345678901234567890123456789\n",
            encoding="utf-8",
        )
        self.assertEqual(provenance.load_repository_names(lock), ["org/repo"])
        output = self.root / "out" / "record.json"
        provenance.atomic_write(output, b"first\n")
        self.assertEqual(output.read_bytes(), b"first\n")
        provenance.atomic_write(output, b"second\n")
        self.assertEqual(output.read_bytes(), b"second\n")

    def test_completed_receipt_revalidates_retained_events_and_self_hashes(self) -> None:
        event_path = self.root / "log" / "build_1" / "events.log"
        event_path.parent.mkdir(parents=True)
        event_path.write_text(
            "[1.000000] (alpha) JobEnded: {'identifier': 'alpha', 'rc': 0}\n"
            "[2.000000] (colcon) EventReactorShutdown: {}\n",
            encoding="utf-8",
        )
        begin = {
            "schema": "ros2-ohos-clean-build-receipt-v1",
            "phase": "BEGIN",
            "created_utc": "2026-01-01T00:00:00Z",
            "configuration": {"selected_rmw": "rmw_fastrtps_cpp"},
        }
        begin["begin_payload_sha256"] = provenance.sha256_bytes(
            provenance.canonical_json(begin)
        )
        begin_bytes = json.dumps(begin, indent=2, sort_keys=True).encode("utf-8") + b"\n"
        complete = dict(begin)
        complete.update(
            {
                "phase": "COMPLETE",
                "completed_utc": "2026-01-01T00:01:00Z",
                "result": "PASS",
                "begin_receipt": {
                    "path": "begin.json",
                    "bytes": len(begin_bytes),
                    "sha256": provenance.sha256_bytes(begin_bytes),
                },
                "colcon": {
                    "events_log": {
                        "path": "log/build_1/events.log",
                        "bytes": event_path.stat().st_size,
                        "sha256": provenance.sha256_file(event_path),
                    },
                    "expected_packages_sha256": provenance.sha256_bytes(b"alpha\n"),
                    "package_count": 1,
                    "packages": ["alpha"],
                    "all_job_return_codes_zero": True,
                    "terminal_event": "EventReactorShutdown",
                },
            }
        )
        complete["complete_payload_sha256"] = provenance.sha256_bytes(
            provenance.canonical_json(complete)
        )
        receipt = self.root / "receipt.json"
        receipt.write_text(json.dumps(complete), encoding="utf-8")
        self.assertEqual(
            "PASS",
            provenance.load_complete_build_receipt(receipt, self.root)["result"],
        )
        complete["colcon"]["package_count"] = 99
        receipt.write_text(json.dumps(complete), encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "complete payload digest mismatch"):
            provenance.load_complete_build_receipt(receipt, self.root)


if __name__ == "__main__":
    unittest.main()
