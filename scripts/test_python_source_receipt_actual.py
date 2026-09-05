#!/usr/bin/env python3
"""Adversarial integration tests for a real source-built Python runtime bundle."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest


WORKSPACE = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(WORKSPACE / "scripts"))

import python_runtime_artifact as artifact_manager  # noqa: E402


DEFAULT_LOCK = WORKSPACE / "scripts" / "python" / "ohos_python.lock.json"
DEFAULT_MANIFEST = (
    WORKSPACE
    / "python_target"
    / "runtime-artifacts"
    / "cpython-3.12.7-ohos-aarch64-source.tar.gz.manifest.json"
)


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


class ActualSourceReceiptTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        requested = os.environ.get("OHOS_PYTHON_SOURCE_ARTIFACT_MANIFEST")
        cls.manifest_path = Path(requested).resolve() if requested else DEFAULT_MANIFEST
        if requested and not cls.manifest_path.is_file():
            raise AssertionError(
                "OHOS_PYTHON_SOURCE_ARTIFACT_MANIFEST was set but is not a file: "
                f"{cls.manifest_path}"
            )
        if not cls.manifest_path.is_file():
            raise unittest.SkipTest(
                "real source runtime artifact is absent; set "
                "OHOS_PYTHON_SOURCE_ARTIFACT_MANIFEST to run the integration gate"
            )
        cls.artifact = artifact_manager.load_json_object(
            cls.manifest_path, "test runtime artifact manifest"
        )
        if cls.artifact.get("provenance_mode") != artifact_manager.SOURCE_MODE:
            raise AssertionError("integration fixture is not source-reproducible")
        archive = cls.manifest_path.parent / cls.artifact["archive"]["filename"]
        receipt = (
            cls.manifest_path.parent
            / cls.artifact["source_build_receipt"]["filename"]
        )
        if not archive.is_file() or not receipt.is_file():
            raise AssertionError("real source runtime bundle is incomplete")
        cls.receipt_path = receipt

    def write_manifest(self, directory: Path, artifact: dict) -> Path:
        path = directory / self.manifest_path.name
        path.write_text(
            json.dumps(artifact, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        return path

    def isolated_workspace(self, receipt_sha256: str | None = None):
        temporary = tempfile.TemporaryDirectory()
        workspace = Path(temporary.name) / "workspace"
        source_lock = artifact_manager.load_json_object(
            WORKSPACE / "scripts" / "python_source" / "source_build.lock.json",
            "test source lock",
        )
        paths = [
            "scripts/python/ohos_python.lock.json",
            "scripts/python_source/source_build.lock.json",
            *source_lock["build_recipe"]["files"],
        ]
        for relative in sorted(set(paths)):
            source = WORKSPACE / Path(*Path(relative).parts)
            destination = workspace / Path(*Path(relative).parts)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
        lock_path = workspace / "scripts" / "python" / "ohos_python.lock.json"
        if receipt_sha256 is not None:
            lock = json.loads(lock_path.read_text(encoding="utf-8"))
            lock["source_build"]["source_build_receipt"]["sha256"] = receipt_sha256
            lock_path.write_text(
                json.dumps(lock, indent=2, sort_keys=True) + "\n", encoding="utf-8"
            )
        return temporary, workspace, lock_path

    def test_real_source_bundle_verifies(self) -> None:
        verified = artifact_manager.verify_source_receipt(
            self.manifest_path, self.artifact, DEFAULT_LOCK
        )
        self.assertEqual(
            verified["sha256"], self.artifact["source_build_receipt"]["sha256"]
        )
        self.assertEqual(Path(verified["path"]), self.receipt_path.resolve())

    def test_unknown_provenance_mode_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory_name:
            artifact = dict(self.artifact)
            artifact["provenance_mode"] = "source-ish"
            manifest = self.write_manifest(Path(directory_name), artifact)
            with self.assertRaisesRegex(
                artifact_manager.ArtifactError, "requires source-reproducible mode"
            ):
                artifact_manager.verify_source_receipt(manifest, artifact, DEFAULT_LOCK)

    def test_unbound_receipt_substitution_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory_name:
            directory = Path(directory_name)
            manifest = self.write_manifest(directory, self.artifact)
            receipt = bytearray(self.receipt_path.read_bytes())
            receipt[-2] = ord(" ") if receipt[-2] != ord(" ") else ord("\t")
            (directory / artifact_manager.SOURCE_RECEIPT_NAME).write_bytes(receipt)
            with self.assertRaisesRegex(
                artifact_manager.ArtifactError, "receipt bytes differ"
            ):
                artifact_manager.verify_source_receipt(
                    manifest, self.artifact, DEFAULT_LOCK
                )

    def test_rebound_false_source_claim_is_rejected(self) -> None:
        fake_receipt = json.loads(self.receipt_path.read_text(encoding="utf-8"))
        fake_receipt["build_recipe_sha256"] = "0" * 64
        payload = (json.dumps(fake_receipt, indent=2, sort_keys=True) + "\n").encode()
        digest = sha256_bytes(payload)
        with tempfile.TemporaryDirectory() as directory_name:
            directory = Path(directory_name)
            artifact = json.loads(json.dumps(self.artifact))
            artifact["source_build_receipt"]["sha256"] = digest
            manifest = self.write_manifest(directory, artifact)
            (directory / artifact_manager.SOURCE_RECEIPT_NAME).write_bytes(payload)
            temporary, _, lock_path = self.isolated_workspace(digest)
            try:
                with self.assertRaisesRegex(
                    artifact_manager.ArtifactError, "recipe hash mismatch"
                ):
                    artifact_manager.verify_source_receipt(manifest, artifact, lock_path)
            finally:
                temporary.cleanup()

    def test_duplicate_key_receipt_is_rejected_after_hash_rebinding(self) -> None:
        raw = self.receipt_path.read_text(encoding="utf-8")
        duplicate = raw.replace("{\n", "{\n  \"schema_version\": 1,\n", 1).encode()
        digest = sha256_bytes(duplicate)
        with tempfile.TemporaryDirectory() as directory_name:
            directory = Path(directory_name)
            artifact = json.loads(json.dumps(self.artifact))
            artifact["source_build_receipt"]["sha256"] = digest
            manifest = self.write_manifest(directory, artifact)
            (directory / artifact_manager.SOURCE_RECEIPT_NAME).write_bytes(duplicate)
            temporary, _, lock_path = self.isolated_workspace(digest)
            try:
                with self.assertRaisesRegex(
                    artifact_manager.ArtifactError, "duplicate JSON key"
                ):
                    artifact_manager.verify_source_receipt(manifest, artifact, lock_path)
            finally:
                temporary.cleanup()

    def test_repository_recipe_single_byte_drift_is_rejected(self) -> None:
        temporary, workspace, lock_path = self.isolated_workspace()
        try:
            source_lock = artifact_manager.load_json_object(
                workspace / "scripts" / "python_source" / "source_build.lock.json",
                "test source lock",
            )
            recipe = workspace / Path(
                *Path(source_lock["build_recipe"]["files"][0]).parts
            )
            recipe.write_bytes(recipe.read_bytes() + b"\n")
            with self.assertRaisesRegex(
                artifact_manager.ArtifactError, "recipe differs from runtime lock"
            ):
                artifact_manager.verify_source_receipt(
                    self.manifest_path, self.artifact, lock_path
                )
        finally:
            temporary.cleanup()


if __name__ == "__main__":
    unittest.main()
