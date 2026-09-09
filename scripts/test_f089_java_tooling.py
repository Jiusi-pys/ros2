#!/usr/bin/env python3
"""Pre-implementation contract tests for F089 Java host tooling.

The test owns no production implementation.  It fixes the O-repository
interface that will validate the immutable Java inputs, perform an offline
clean replay, and exercise the pinned tooling through its real ros2_java
consumer once Main has materialized the AJ/GJ/J source repositories.
"""

from __future__ import annotations

from contextlib import contextmanager
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from typing import Any, Iterator


ROOT = Path(__file__).resolve().parents[1]
LOCK = ROOT / "scripts" / "java" / "host_tooling.lock.json"
RUNNER = ROOT / "scripts" / "java_tooling.py"
CACHE = ROOT / "out" / "f089-java-tooling-cache"

APPROVAL_BUNDLE_SHA256 = (
    "D63735E9CDFB485D789358B34081F0193897BE378E35402E8921114703E30A82"
)
HEX40 = re.compile(r"^[0-9a-f]{40}$")
HEX64 = re.compile(r"^[0-9a-fA-F]{64}$")

EXPECTED_SOURCES = {
    "ros2_java_seed": {
        "url": "https://github.com/EGAlberts/ros2_java.git",
        "commit": "9f344d7657e1d2a81f872957eafdb1539f08eb9b",
        "workspace_path": "src/Jiusi-pys/ros2_java",
    },
    "ros2_java_upstream": {
        "url": "https://github.com/ros2-java/ros2_java.git",
        "commit": "d0e4e952bad1977ce9818d00b520d3ad0aeafd8d",
        "workspace_path": None,
    },
    "ament_java": {
        "url": "https://github.com/ros2-java/ament_java.git",
        "commit": "c430324f0e8aa12106e67799795994b8aede10a9",
        "workspace_path": "src/ros2-java/ament_java",
    },
    "ament_gradle_plugin": {
        "url": "https://github.com/EGAlberts/ament_gradle_plugin.git",
        "commit": "f1addd56b7ae1d8182e0aac23759313d5f354894",
        "workspace_path": "src/Jiusi-pys/ament_gradle_plugin",
    },
    "ros2_java_examples": {
        "url": "https://github.com/EGAlberts/ros2_java_examples.git",
        "commit": "7e965265c17c81ab28f3c93b84f2af8c60da5b7c",
        "workspace_path": "src/Jiusi-pys/ros2_java_examples",
    },
}


def _load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


@contextmanager
def _temporary_directory(prefix: str) -> Iterator[Path]:
    """Remove Gradle outputs after its Windows single-use daemon releases logs."""
    path = Path(tempfile.mkdtemp(prefix=prefix))
    try:
        yield path
    finally:
        deadline = time.monotonic() + 20
        while path.exists():
            try:
                shutil.rmtree(path)
            except PermissionError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.25)


def _last_json(stdout: str) -> dict[str, Any]:
    for line in reversed(stdout.splitlines()):
        line = line.strip()
        if line:
            return json.loads(line)
    raise AssertionError("Java tooling command emitted no JSON result")


def _run_tooling(*arguments: str, timeout: int = 600) -> dict[str, Any]:
    if not RUNNER.is_file():
        raise AssertionError(f"missing production tooling runner: {RUNNER}")
    result = subprocess.run(
        [sys.executable, str(RUNNER), *arguments],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=timeout,
        check=False,
    )
    if result.returncode != 0:
        raise AssertionError(
            f"Java tooling command failed with exit {result.returncode}:\n{result.stdout}"
        )
    return _last_json(result.stdout)


def _git(repo: Path, *arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", "-C", str(repo), *arguments],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=30,
        check=False,
    )


def _assert_materialized_from_seed(test: unittest.TestCase, name: str) -> Path:
    expected = EXPECTED_SOURCES[name]
    relative = expected["workspace_path"]
    test.assertIsNotNone(relative)
    repo = ROOT / str(relative)
    test.assertTrue(repo.is_dir(), f"missing required source repository: {repo}")
    test.assertTrue((repo / ".git").exists(), f"source is not a Git repository: {repo}")
    commit = str(expected["commit"])
    present = _git(repo, "cat-file", "-e", f"{commit}^{{commit}}")
    test.assertEqual(present.returncode, 0, present.stdout)
    ancestry = _git(repo, "merge-base", "--is-ancestor", commit, "HEAD")
    test.assertEqual(
        ancestry.returncode,
        0,
        f"{name} HEAD is not derived from approved seed {commit}:\n{ancestry.stdout}",
    )
    return repo


def _source_states(repositories: list[Path]) -> dict[str, str]:
    states: dict[str, str] = {}
    for repo in repositories:
        result = _git(repo, "status", "--porcelain=v1", "--untracked-files=all")
        if result.returncode != 0:
            raise AssertionError(result.stdout)
        states[repo.as_posix()] = result.stdout
    return states


class JavaToolingProvenanceTest(unittest.TestCase):
    """TEST-F089-01: exact source/JDK/Gradle provenance."""

    def test_exact_source_tool_and_artifact_provenance(self) -> None:
        self.assertTrue(LOCK.is_file(), f"missing Java tooling lock: {LOCK}")
        lock = _load_json(LOCK)
        self.assertEqual(lock.get("schema"), "mdds.java-host-tooling-lock/v1")
        self.assertEqual(
            lock.get("approval_bundle_sha256"), APPROVAL_BUNDLE_SHA256
        )

        sources = lock.get("sources")
        self.assertIsInstance(sources, dict)
        self.assertEqual(set(sources), set(EXPECTED_SOURCES))
        for name, expected in EXPECTED_SOURCES.items():
            with self.subTest(source=name):
                actual = sources[name]
                self.assertEqual(actual.get("url"), expected["url"])
                self.assertEqual(actual.get("commit"), expected["commit"])
                self.assertRegex(actual["commit"], HEX40)
                self.assertEqual(
                    actual.get("workspace_path"), expected["workspace_path"]
                )

        tools = lock.get("tools")
        self.assertIsInstance(tools, dict)
        self.assertEqual(tools.get("jdk", {}).get("version"), "jdk-21.0.12+8")
        self.assertEqual(tools.get("gradle", {}).get("version"), "8.5.0")
        self.assertIn(
            tools.get("jdk", {}).get("distribution"),
            {"Temurin", "OpenJDK", "Temurin/OpenJDK"},
        )
        for name in ("jdk", "gradle"):
            with self.subTest(tool=name):
                artifact = tools[name].get("artifact")
                self.assertIsInstance(artifact, dict)
                self.assertTrue(artifact.get("filename"))
                self.assertTrue(str(artifact.get("url", "")).startswith("https://"))
                self.assertRegex(str(artifact.get("sha256", "")), HEX64)

        dependency_locks = lock.get("dependency_locks")
        self.assertIsInstance(dependency_locks, list)
        self.assertTrue(dependency_locks, "transitive dependency locks must be recorded")
        seen_dependency_locks: set[str] = set()
        for dependency_lock in dependency_locks:
            relative = str(dependency_lock.get("path", ""))
            self.assertTrue(relative)
            self.assertNotIn(relative, seen_dependency_locks)
            seen_dependency_locks.add(relative)
            path = Path(relative)
            self.assertFalse(path.is_absolute())
            self.assertNotIn("..", path.parts)
            resolved = ROOT / path
            self.assertTrue(resolved.is_file(), f"missing dependency lock: {resolved}")
            expected_sha256 = str(dependency_lock.get("sha256", ""))
            self.assertRegex(expected_sha256, HEX64)
            self.assertEqual(_sha256(resolved), expected_sha256.lower())


class JavaToolingReplayTest(unittest.TestCase):
    """TEST-F089-02: offline clean replay is deterministic and source-safe."""

    def test_two_offline_clean_replays_are_identical(self) -> None:
        repositories = [
            _assert_materialized_from_seed(self, "ament_java"),
            _assert_materialized_from_seed(self, "ament_gradle_plugin"),
        ]
        before = _source_states(repositories)
        receipts: list[dict[str, Any]] = []
        with _temporary_directory(prefix="f089-java-replay-") as root:
            for index in range(2):
                receipt = _run_tooling(
                    "replay",
                    "--workspace",
                    str(ROOT),
                    "--lock",
                    str(LOCK),
                    "--source-root",
                    str(ROOT / "src"),
                    "--cache-dir",
                    str(CACHE),
                    "--output-dir",
                    str(root / f"replay-{index + 1}"),
                    "--offline",
                    "--clean",
                    "--json",
                )
                self.assertEqual(receipt.get("schema"), "mdds.java-tooling-replay/v1")
                self.assertEqual(receipt.get("result"), "PASS")
                self.assertIs(receipt.get("offline"), True)
                self.assertIs(receipt.get("clean"), True)
                self.assertIs(receipt.get("network_accessed"), False)
                self.assertRegex(
                    str(receipt.get("input_fingerprint_sha256", "")), HEX64
                )
                self.assertRegex(
                    str(receipt.get("artifact_tree_sha256", "")), HEX64
                )
                receipts.append(receipt)

        self.assertEqual(
            receipts[0]["input_fingerprint_sha256"],
            receipts[1]["input_fingerprint_sha256"],
        )
        self.assertEqual(
            receipts[0]["artifact_tree_sha256"],
            receipts[1]["artifact_tree_sha256"],
        )
        self.assertEqual(before, _source_states(repositories))


class JavaToolingConsumerTest(unittest.TestCase):
    """TEST-F089-03: the pinned tooling builds its actual ros2_java consumer."""

    def test_pinned_ros2_java_consumer(self) -> None:
        repositories = [
            _assert_materialized_from_seed(self, "ros2_java_seed"),
            _assert_materialized_from_seed(self, "ament_java"),
            _assert_materialized_from_seed(self, "ament_gradle_plugin"),
        ]
        before = _source_states(repositories)
        with _temporary_directory(prefix="f089-java-consumer-") as directory:
            receipt = _run_tooling(
                "consumer",
                "--consumer",
                "ros2_java_seed",
                "--workspace",
                str(ROOT),
                "--lock",
                str(LOCK),
                "--source-root",
                str(ROOT / "src"),
                "--cache-dir",
                str(CACHE),
                "--output-dir",
                str(directory),
                "--offline",
                "--clean",
                "--json",
            )
        self.assertEqual(receipt.get("schema"), "mdds.java-tooling-consumer/v1")
        self.assertEqual(receipt.get("result"), "PASS")
        self.assertEqual(receipt.get("consumer"), "ros2_java_seed")
        self.assertEqual(receipt.get("jdk_version"), "jdk-21.0.12+8")
        self.assertEqual(receipt.get("gradle_version"), "8.5.0")
        self.assertIs(receipt.get("offline"), True)
        self.assertIs(receipt.get("clean"), True)
        self.assertIs(receipt.get("network_accessed"), False)
        self.assertRegex(str(receipt.get("artifact_tree_sha256", "")), HEX64)
        tasks = receipt.get("executed_tasks")
        self.assertIsInstance(tasks, list)
        self.assertTrue(tasks, "consumer receipt must identify the executed Gradle tasks")
        self.assertEqual(before, _source_states(repositories))


if __name__ == "__main__":
    unittest.main()
