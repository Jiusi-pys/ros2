#!/usr/bin/env python3
"""Focused regression tests for freeze_verification_manifest.py."""

from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


SCRIPT = Path(__file__).with_name("freeze_verification_manifest.py")
SPEC = importlib.util.spec_from_file_location("freeze_verification_manifest", SCRIPT)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot import collector: {SCRIPT}")
freezer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(freezer)


def git(repo: Path, *args: str) -> None:
    subprocess.run(
        ["git", *args],
        cwd=repo,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def make_repo(root: Path, name: str) -> Path:
    repo = root / name
    repo.mkdir()
    git(repo, "init")
    git(repo, "config", "user.name", "Manifest Test")
    git(repo, "config", "user.email", "manifest-test@example.invalid")
    (repo / "tracked.txt").write_text("original\n", encoding="utf-8")
    git(repo, "add", "tracked.txt")
    git(repo, "commit", "-m", "initial")
    return repo


class OutputBoundaryTests(unittest.TestCase):
    def test_rejects_output_nested_in_existing_manifest_bundle(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            frozen = root / "p1_dsb_20260902T083610_3f2c_final2"
            frozen.mkdir()
            (frozen / "manifest.v1.json").write_text("{}\n", encoding="utf-8")
            candidate = frozen / "new_round"

            with self.assertRaisesRegex(RuntimeError, "existing verification manifest bundle"):
                freezer.prepare_output_path(str(candidate))

            self.assertFalse(candidate.exists())

    def test_allows_sibling_of_existing_manifest_bundle(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            frozen = root / "final2"
            frozen.mkdir()
            (frozen / "manifest.v1.json").write_text("{}\n", encoding="utf-8")
            candidate = root / "new_round"

            self.assertEqual(freezer.prepare_output_path(str(candidate)), candidate)


class GitStabilityTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.repo = make_repo(self.root, "repo")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def make_collection_fixture(self) -> tuple[list[str], Path, Path]:
        ros2 = make_repo(self.root, "ros2")
        mdds = make_repo(self.root, "mdds")
        rmw_mdds = make_repo(self.root, "rmw_mdds")
        inputs = self.root / "inputs"
        logs = self.root / "logs"
        artifacts = self.root / "artifacts"
        inputs.mkdir()
        logs.mkdir()
        artifacts.mkdir()

        evidence_inputs: dict[str, Path] = {}
        for name in sorted(freezer.REQUIRED_INPUT_NAMES):
            if name.endswith("_evidence") or name == "raw_announce":
                evidence = inputs / name
                evidence.mkdir()
                (evidence / "evidence.txt").write_text(f"{name} evidence\n", encoding="utf-8")
            else:
                evidence = inputs / f"{name}.txt"
                evidence.write_text(f"{name}\n", encoding="utf-8")
            evidence_inputs[name] = evidence
        profile = inputs / "mdds_gateway_ohos_dsoftbus.conf"
        profile.write_text("profile\n", encoding="utf-8")

        raw_logs: dict[str, Path] = {}
        command_logs: dict[str, Path] = {}
        for name in sorted(freezer.REQUIRED_RAW_LOG_NAMES):
            raw = logs / f"raw-{name}.log"
            command = logs / f"command-{name}.log"
            raw.write_text(f"raw {name}\n", encoding="utf-8")
            command.write_text(f"command {name}\n", encoding="utf-8")
            raw_logs[name] = raw
            command_logs[name] = command

        artifact_paths = {
            "libmdds": artifacts / "libmdds.so",
            "librmw_mdds": artifacts / "librmw_mdds.so",
            "mdds_gateway": artifacts / "mdds_gateway",
        }
        for artifact in artifact_paths.values():
            artifact.write_bytes(b"artifact\n")

        out = self.root / "new_round"
        argv = [
            str(SCRIPT),
            "--run-id", "stability-regression",
            "--out", str(out),
            "--repo", f"ros2={ros2}",
            "--repo", f"mdds={mdds}",
            "--repo", f"rmw_mdds={rmw_mdds}",
            "--gateway-profile", str(profile),
        ]
        for name in sorted(evidence_inputs):
            argv.extend(("--input", f"{name}={evidence_inputs[name]}"))
        for name in sorted(raw_logs):
            argv.extend(("--raw-log", f"{name}={raw_logs[name]}"))
        for name in sorted(artifact_paths):
            argv.extend(("--artifact", f"{name}={artifact_paths[name]}"))
        for name in sorted(command_logs):
            argv.extend(("--command-log", f"{name}={command_logs[name]}"))
        return argv, out, ros2

    def test_accepts_unchanged_repository_state(self) -> None:
        before = freezer.capture_git_state(self.repo)
        after = freezer.capture_git_state(self.repo)

        self.assertEqual(freezer.changed_git_state_fields(before, after), [])
        freezer.assert_git_state_stable("mdds", before, after)

    def test_detects_head_change(self) -> None:
        before = freezer.capture_git_state(self.repo)
        (self.repo / "tracked.txt").write_text("second commit\n", encoding="utf-8")
        git(self.repo, "add", "tracked.txt")
        git(self.repo, "commit", "-m", "second")

        after = freezer.capture_git_state(self.repo)
        self.assertIn("head", freezer.changed_git_state_fields(before, after))
        with self.assertRaisesRegex(RuntimeError, "head"):
            freezer.assert_git_state_stable("mdds", before, after)

    def test_detects_tracked_diff_change(self) -> None:
        before = freezer.capture_git_state(self.repo)
        (self.repo / "tracked.txt").write_text("changed\n", encoding="utf-8")

        after = freezer.capture_git_state(self.repo)
        changed = freezer.changed_git_state_fields(before, after)
        self.assertIn("status_porcelain_z", changed)
        self.assertIn("diff_head_binary", changed)
        self.assertIn("diff_unstaged_binary", changed)
        with self.assertRaisesRegex(RuntimeError, "diff_head_binary"):
            freezer.assert_git_state_stable("mdds", before, after)

    def test_detects_untracked_content_change_with_same_path_and_size(self) -> None:
        untracked = self.repo / "untracked.txt"
        untracked.write_text("alpha\n", encoding="utf-8")
        before = freezer.capture_git_state(self.repo)
        untracked.write_text("omega\n", encoding="utf-8")

        after = freezer.capture_git_state(self.repo)
        changed = freezer.changed_git_state_fields(before, after)
        self.assertEqual(changed, ["untracked_inventory"])
        with self.assertRaisesRegex(RuntimeError, "untracked_inventory"):
            freezer.assert_git_state_stable("mdds", before, after)

    def test_main_seals_unchanged_repositories(self) -> None:
        argv, out, _ = self.make_collection_fixture()
        with (
            mock.patch.object(sys, "argv", argv),
            contextlib.redirect_stdout(io.StringIO()),
            contextlib.redirect_stderr(io.StringIO()),
        ):
            self.assertEqual(freezer.main(), 0)

        manifest = json.loads((out / "manifest.v1.json").read_text(encoding="utf-8"))
        self.assertEqual(
            {item["name"] for item in manifest["copied"]["inputs"]},
            freezer.REQUIRED_INPUT_NAMES,
        )
        stability = manifest["repository_stability"]
        self.assertEqual(stability["result"], "PER_REPOSITORY_SEQUENTIAL_RECHECK_MATCHED")
        self.assertEqual(stability["scope"], "per-repository")
        self.assertEqual(stability["capture_order"], ["mdds", "rmw_mdds", "ros2"])
        self.assertEqual(stability["recheck_order"], ["mdds", "rmw_mdds", "ros2"])
        self.assertFalse(stability["cross_repository_snapshot_atomic"])
        self.assertFalse(stability["cross_repository_write_lock_held"])
        self.assertIn("No cross-repository write lock was held", stability["warning"])
        self.assertTrue((out.parent / f"{out.name}.tar").is_file())

    def test_main_rejects_each_missing_required_input(self) -> None:
        argv, _, _ = self.make_collection_fixture()
        for missing_name in sorted(freezer.REQUIRED_INPUT_NAMES):
            missing_argv = list(argv)
            missing_assignment = next(
                entry for entry in missing_argv if entry.startswith(f"{missing_name}=")
            )
            missing_index = missing_argv.index(missing_assignment)
            self.assertEqual(missing_argv[missing_index - 1], "--input")
            del missing_argv[missing_index - 1:missing_index + 1]
            out = self.root / f"missing-{missing_name}"
            missing_argv[missing_argv.index("--out") + 1] = str(out)
            stderr = io.StringIO()
            with (
                mock.patch.object(sys, "argv", missing_argv),
                contextlib.redirect_stdout(io.StringIO()),
                contextlib.redirect_stderr(stderr),
                self.assertRaises(SystemExit) as raised,
            ):
                freezer.main()
            self.assertEqual(raised.exception.code, 2)
            self.assertIn(missing_name, stderr.getvalue())
            self.assertFalse(out.exists())

    def test_main_accepts_optional_domain0_smoke_input(self) -> None:
        argv, out, _ = self.make_collection_fixture()
        domain0_smoke = self.root / "domain0_smoke"
        domain0_smoke.mkdir()
        (domain0_smoke / "evidence.txt").write_text("domain 0 smoke\n", encoding="utf-8")
        argv.extend(("--input", f"domain0_smoke={domain0_smoke}"))
        with (
            mock.patch.object(sys, "argv", argv),
            contextlib.redirect_stdout(io.StringIO()),
            contextlib.redirect_stderr(io.StringIO()),
        ):
            self.assertEqual(freezer.main(), 0)

        manifest = json.loads((out / "manifest.v1.json").read_text(encoding="utf-8"))
        self.assertEqual(
            {item["name"] for item in manifest["copied"]["inputs"]},
            freezer.REQUIRED_INPUT_NAMES | freezer.OPTIONAL_EXECUTED_INPUT_NAMES,
        )

    def test_main_aborts_before_manifest_when_a_repository_changes(self) -> None:
        argv, out, ros2 = self.make_collection_fixture()

        original_copy = freezer.copy_input
        changed = False

        def copy_then_change(*args: object, **kwargs: object) -> dict[str, object]:
            nonlocal changed
            result = original_copy(*args, **kwargs)
            if not changed:
                (ros2 / "tracked.txt").write_text("changed during collection\n", encoding="utf-8")
                changed = True
            return result

        with (
            mock.patch.object(freezer, "copy_input", side_effect=copy_then_change),
            mock.patch.object(sys, "argv", argv),
            contextlib.redirect_stdout(io.StringIO()),
            contextlib.redirect_stderr(io.StringIO()),
        ):
            self.assertEqual(freezer.main(), 2)

        self.assertTrue(changed)
        self.assertFalse((out / "manifest.v1.json").exists())


if __name__ == "__main__":
    unittest.main()
