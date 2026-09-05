#!/usr/bin/env python3
"""Verify the deployed Python inputs against the ROS release build receipt."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import stat


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def sha(path: Path) -> str:
    require(stat.S_ISREG(path.lstat().st_mode), f"not a regular file: {path}")
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def document(path: Path) -> dict:
    sha(path)
    value = json.loads(path.read_text(encoding="utf-8"))
    require(isinstance(value, dict), f"not a JSON object: {path}")
    return value


def verify(prefix: Path, expected: dict, board: str | None, full: bool) -> dict:
    require(re.fullmatch(r"/data/[A-Za-z0-9._+-]+", str(prefix)) is not None,
            "invalid Python deployment prefix")
    require(prefix.is_dir() and not prefix.is_symlink(), "Python prefix is missing or linked")
    overlay = prefix / "ros2-site-packages"
    require(overlay.is_dir() and not overlay.is_symlink(), "Python overlay is missing or linked")
    runtime = document(prefix / "PYTHON_RUNTIME_DEPLOYMENT.json")
    artifact = document(prefix / "PYTHON_RUNTIME_ARTIFACT.manifest.json")
    marker = document(overlay / ".ros2-ohos-python-deployment.json")
    pairs = {
        "python_lock_sha256": expected["lock"]["sha256"],
        "python_runtime_archive_sha256": expected["runtime_archive_sha256"],
        "python_runtime_tree_sha256": expected["runtime_tree_sha256"],
        "runtime_provenance_mode": expected["provenance_mode"],
        "remote_runtime_prefix": str(prefix),
        "complete": True,
    }
    for key, value in pairs.items():
        require(runtime.get(key) == value, f"runtime marker mismatch: {key}")
        require(marker.get(key) == value, f"overlay marker mismatch: {key}")
    if board:
        require(runtime.get("board_serial") == board, "runtime belongs to another board")
        require(marker.get("board_serial") == board, "overlay belongs to another board")
    for key, value in {
        "remote_overlay": str(overlay),
        "python_runtime_interface_sha256": expected["runtime_interface_sha256"],
        "python_stage_marker_sha256": expected["stage_marker"]["sha256"],
        "python_stage_tree_sha256": expected["stage_tree_sha256"],
    }.items():
        require(marker.get(key) == value, f"overlay marker mismatch: {key}")
    for key, value in {
        "python_lock_sha256": expected["lock"]["sha256"],
        "runtime_tree_sha256": expected["runtime_tree_sha256"],
        "runtime_entry_count": expected["runtime_entry_count"],
        "runtime_payload_bytes": expected["runtime_payload_bytes"],
        "provenance_mode": expected["provenance_mode"],
    }.items():
        require(artifact.get(key) == value, f"runtime artifact mismatch: {key}")
    require(artifact.get("archive", {}).get("sha256") == expected["runtime_archive_sha256"],
            "runtime archive identity mismatch")
    if expected["provenance_mode"] == "source-reproducible":
        source_path = prefix / "PYTHON_SOURCE_BUILD_RECEIPT.json"
        source_sha = expected["source_build_receipt"]["sha256"]
        require(sha(source_path) == source_sha, "Python source-build receipt bytes differ")
        source = document(source_path)
        require(artifact.get("source_build_receipt", {}).get("sha256") == source_sha,
                "artifact source-build receipt differs")
        require(source.get("python_source_lock_sha256") == expected["source_lock_sha256"],
                "Python source lock differs")
        require(source.get("build_recipe_sha256") == expected["source_build_recipe_sha256"],
                "Python source recipe differs")
        for control in (runtime, marker):
            for key, value in {
                "python_source_build_receipt_sha256": source_sha,
                "python_source_lock_sha256": expected["source_lock_sha256"],
                "python_source_build_recipe_sha256": expected["source_build_recipe_sha256"],
            }.items():
                require(control.get(key) == value, f"source-build marker mismatch: {key}")
    require(sha(prefix / "usr/lib/libpython3.12.so.1.0") == expected["runtime_library_sha256"],
            "libpython differs from the build input")
    stage_path = overlay / ".ros2-ohos-python-stage.json"
    require(sha(stage_path) == expected["stage_marker"]["sha256"], "stage marker bytes differ")
    stage = document(stage_path)
    require(stage.get("stage_tree_sha256") == expected["stage_tree_sha256"], "stage tree differs")
    manifest_path = overlay / ".ros2-ohos-python-files.sha256"
    inventory_path = overlay / ".ros2-ohos-python-paths.txt"
    require(sha(manifest_path) == marker["python_overlay_file_manifest_sha256"],
            "overlay file manifest differs")
    require(sha(inventory_path) == marker["python_overlay_path_inventory_sha256"],
            "overlay inventory differs")
    if full:
        module_path = Path(__file__).with_name("python_runtime_artifact.py")
        spec = importlib.util.spec_from_file_location("runtime_digest", module_path)
        require(spec is not None and spec.loader is not None, "runtime verifier is missing")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        actual = module.local_tree_digest(prefix / "usr")
        require(actual == (expected["runtime_tree_sha256"], expected["runtime_entry_count"],
                           expected["runtime_payload_bytes"]), "runtime full tree differs")
        actual_paths = []
        regular_files = set()
        for path in overlay.rglob("*"):
            mode = path.lstat().st_mode
            relative = path.relative_to(overlay).as_posix()
            require("\n" not in relative and "\r" not in relative, "invalid overlay path")
            require(stat.S_ISREG(mode) or stat.S_ISDIR(mode), f"special overlay entry: {relative}")
            actual_paths.append(("d " if stat.S_ISDIR(mode) else "f ") + relative)
            if stat.S_ISREG(mode):
                regular_files.add(relative)
        recorded_paths = inventory_path.read_text(encoding="utf-8").splitlines()
        require(sorted(actual_paths) == sorted(recorded_paths), "overlay path inventory differs")
        checked = set()
        for line in manifest_path.read_text(encoding="utf-8").splitlines():
            match = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
            require(match is not None, "invalid overlay checksum line")
            digest, name = match.groups()
            require(name in regular_files and name not in checked, "invalid/duplicate manifest path")
            require(sha(overlay / name) == digest, f"overlay bytes differ: {name}")
            checked.add(name)
        controls = {manifest_path.name, ".ros2-ohos-python-deployment.json"}
        require(checked == regular_files - controls, "overlay manifest does not cover every payload file")
    return {"board": board, "prefix": str(prefix), "full_tree": full,
            "runtime_archive_sha256": expected["runtime_archive_sha256"],
            "runtime_tree_sha256": expected["runtime_tree_sha256"],
            "stage_tree_sha256": expected["stage_tree_sha256"],
            "runtime_marker_sha256": sha(prefix / "PYTHON_RUNTIME_DEPLOYMENT.json"),
            "overlay_marker_sha256": sha(overlay / ".ros2-ohos-python-deployment.json")}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--receipt", type=Path, required=True)
    parser.add_argument("--receipt-sha256", required=True)
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--board")
    parser.add_argument("--full-tree", action="store_true")
    args = parser.parse_args()
    require(sha(args.receipt) == args.receipt_sha256, "ROS clean-build receipt bytes differ")
    expected = document(args.receipt)["inputs"]["python"]
    result = verify(args.prefix, expected, args.board, args.full_tree)
    print("PYTHON_RELEASE_VERIFIED " + json.dumps(result, sort_keys=True))


if __name__ == "__main__":
    main()
