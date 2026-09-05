#!/usr/bin/env python3
"""Create and close a tamper-evident receipt for one clean OHOS ROS build."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import re
import sys
from pathlib import Path
from typing import Any

import collect_ohos_release_provenance as provenance
import python_runtime_artifact
import python_target


SCHEMA = "ros2-ohos-clean-build-receipt-v1"
JOB_ENDED = re.compile(
    r"^\[[^]]+\] \(([^)]*)\) JobEnded: \{'identifier': '([^']+)', 'rc': (-?[0-9]+)\}$",
    re.MULTILINE,
)


class ReceiptError(RuntimeError):
    pass


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace(
        "+00:00", "Z"
    )


def regular(path: Path, label: str) -> Path:
    resolved = path.resolve()
    if not resolved.is_file() or resolved.is_symlink():
        raise ReceiptError(f"{label} must be a non-symlink regular file: {resolved}")
    return resolved


def source_snapshot(workspace: Path, lock: Path) -> tuple[dict[str, Any], str]:
    names = provenance.load_repository_names(lock)
    states = [provenance.repository_state(workspace, "ros2-meta")]
    states.extend(
        provenance.repository_state(workspace / "src" / name, name) for name in names
    )
    for captured in states:
        repo = workspace if captured["name"] == "ros2-meta" else workspace / "src" / captured["name"]
        current = provenance.repository_state(repo, captured["name"])
        if current["state_sha256"] != captured["state_sha256"]:
            raise ReceiptError(f"source changed while build inputs were captured: {captured['name']}")
    payload = {"meta": states[0], "repositories": states[1:]}
    return payload, provenance.sha256_bytes(provenance.canonical_json(payload))


def file_record(path: Path, workspace: Path) -> dict[str, Any]:
    path = regular(path, "build input")
    try:
        label = path.relative_to(workspace).as_posix()
    except ValueError:
        label = str(path)
    return {"path": label, "bytes": path.stat().st_size, "sha256": provenance.sha256_file(path)}


def path_label(path: Path, workspace: Path) -> str:
    resolved = path.resolve()
    try:
        return resolved.relative_to(workspace).as_posix()
    except ValueError:
        return str(resolved)


def recorded_path(label: str, workspace: Path) -> Path:
    path = Path(label)
    return path if path.is_absolute() else workspace / path


def checked_json(path: Path, label: str) -> dict[str, Any]:
    path = regular(path, label)
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ReceiptError(f"invalid {label}: {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ReceiptError(f"{label} must contain a JSON object: {path}")
    return value


def read_expected_packages(path: Path) -> list[str]:
    path = regular(path, "expected package list")
    names = path.read_text(encoding="utf-8").splitlines()
    if not names or names != sorted(set(names)):
        raise ReceiptError("expected package list must be non-empty, unique and byte-sorted")
    if any(not re.fullmatch(r"[A-Za-z0-9_.+-]+", name) for name in names):
        raise ReceiptError("expected package list contains an unsafe package name")
    return names


def validate_python_inputs(
    runtime_manifest_path: Path,
    runtime_archive_path: Path,
    stage_marker_path: Path,
    lock_path: Path,
    target_root: Path,
) -> dict[str, Any]:
    runtime_manifest = checked_json(runtime_manifest_path, "Python runtime manifest")
    if runtime_manifest.get("provenance_mode") != "source-reproducible":
        raise ReceiptError("clean ROS release requires a verified Python source-build receipt")
    source_fields = {
        "source_build_receipt": None,
        "source_lock_sha256": None,
        "source_build_recipe_sha256": None,
    }
    if runtime_manifest.get("provenance_mode") == "source-reproducible":
        verified_source = python_runtime_artifact.verify_source_receipt(
            runtime_manifest_path, runtime_manifest, lock_path,
        )
        source_record = verified_source["receipt"]
        source_fields = {
            "source_build_receipt": file_record(
                Path(verified_source["path"]), runtime_manifest_path.resolve().parent,
            ),
            "source_lock_sha256": source_record["python_source_lock_sha256"],
            "source_build_recipe_sha256": source_record["build_recipe_sha256"],
        }
    stage_marker = checked_json(stage_marker_path, "Python dependency stage marker")
    lock_path = regular(lock_path, "Python lock")
    lock = python_target.load_lock(lock_path)
    lock_sha = provenance.sha256_file(lock_path)
    if runtime_manifest.get("python_lock_sha256") != lock_sha:
        raise ReceiptError("Python runtime manifest is not bound to the selected Python lock")
    if stage_marker.get("lock_sha256") != lock_sha or stage_marker.get("complete") is not True:
        raise ReceiptError("Python dependency stage is not complete or uses another lock")
    archive = runtime_manifest.get("archive")
    if not isinstance(archive, dict) or not re.fullmatch(
        r"[0-9a-f]{64}", str(archive.get("sha256", ""))
    ):
        raise ReceiptError("Python runtime manifest lacks a valid archive SHA-256")
    runtime_archive_path = regular(runtime_archive_path, "Python runtime archive")
    if archive.get("filename") != runtime_archive_path.name:
        raise ReceiptError("Python runtime archive name differs from its manifest")
    if provenance.sha256_file(runtime_archive_path) != archive.get("sha256"):
        raise ReceiptError("Python runtime archive bytes differ from its manifest")
    tree_sha = str(runtime_manifest.get("runtime_tree_sha256", ""))
    if not re.fullmatch(r"[0-9a-f]{64}", tree_sha):
        raise ReceiptError("Python runtime manifest lacks a valid runtime tree SHA-256")
    stage_tree_sha = str(stage_marker.get("stage_tree_sha256", ""))
    if not re.fullmatch(r"[0-9a-f]{64}", stage_tree_sha):
        raise ReceiptError("Python dependency stage marker lacks a valid tree SHA-256")
    for field in ("runtime_entry_count", "runtime_payload_bytes"):
        if not isinstance(runtime_manifest.get(field), int) or runtime_manifest[field] < 1:
            raise ReceiptError(f"Python runtime manifest lacks a valid {field}")
    archive_tree_sha, archive_entries, archive_bytes, _ = python_runtime_artifact.archive_tree_digest(
        runtime_archive_path
    )
    if (
        archive_tree_sha != tree_sha
        or archive_entries != runtime_manifest.get("runtime_entry_count")
        or archive_bytes != runtime_manifest.get("runtime_payload_bytes")
    ):
        raise ReceiptError("Python runtime archive semantic tree differs from its manifest")
    python_target.verify_runtime(target_root, lock)
    python_target.verify_stage(stage_marker_path.parent, lock, lock_path)
    runtime_interface_sha = provenance.sha256_bytes(
        json.dumps(lock["runtime_interface"], sort_keys=True, separators=(",", ":")).encode(
            "utf-8"
        )
    )
    runtime_library_sha = next(
        (
            item.get("sha256")
            for item in lock["runtime_interface"]["files"]
            if item.get("path") == "lib/libpython3.12.so.1.0"
        ),
        None,
    )
    if not isinstance(runtime_library_sha, str) or not re.fullmatch(
        r"[0-9a-f]{64}", runtime_library_sha
    ):
        raise ReceiptError("Python lock lacks the target libpython identity")
    return {
        "lock": file_record(lock_path, lock_path.resolve().parents[2]),
        "runtime_manifest": file_record(runtime_manifest_path, runtime_manifest_path.resolve().parent),
        "runtime_archive_sha256": archive["sha256"],
        "runtime_archive_bytes": runtime_archive_path.stat().st_size,
        "runtime_tree_sha256": tree_sha,
        "runtime_entry_count": runtime_manifest.get("runtime_entry_count"),
        "runtime_payload_bytes": runtime_manifest.get("runtime_payload_bytes"),
        "runtime_interface_sha256": runtime_interface_sha,
        "runtime_library_sha256": runtime_library_sha,
        "provenance_mode": runtime_manifest.get("provenance_mode", "UNKNOWN"),
        "stage_marker": file_record(stage_marker_path, stage_marker_path.resolve().parent),
        "stage_tree_sha256": stage_tree_sha,
        **source_fields,
    }


def begin(args: argparse.Namespace) -> None:
    workspace = args.workspace.resolve()
    lock = regular(args.lock if args.lock.is_absolute() else workspace / args.lock, "ROS lock")
    install = args.install_root.resolve()
    if not install.is_dir() or install.is_symlink():
        raise ReceiptError(f"dependency install prefix is missing: {install}")
    output = args.output.resolve()
    if output.exists() or output.is_symlink():
        raise ReceiptError(f"begin receipt output is create-only: {output}")
    source, source_sha = source_snapshot(workspace, lock)
    sdk = provenance.sdk_fingerprint(args.sdk_root)
    python_inputs = validate_python_inputs(
        args.python_runtime_manifest.resolve(),
        args.python_runtime_archive.resolve(),
        args.python_stage_marker.resolve(),
        args.python_lock.resolve(),
        args.python_target_root.resolve(),
    )
    python_inputs["paths"] = {
        "lock": path_label(args.python_lock, workspace),
        "runtime_manifest": path_label(args.python_runtime_manifest, workspace),
        "runtime_archive": path_label(args.python_runtime_archive, workspace),
        "stage_marker": path_label(args.python_stage_marker, workspace),
        "target_root": path_label(args.python_target_root, workspace),
    }
    input_paths = [
        lock,
        workspace / "pixi.lock",
        workspace / "scripts" / "build_ohos.sh",
        workspace / "scripts" / "ohos_build_receipt.py",
        workspace / "cmake" / "ohos-aarch64.toolchain.cmake",
        workspace / "target_deps_src" / "sources.lock",
        install / ".ohos-target-deps.clean-receipt.json",
        install / ".ohos-target-deps.clean-prefix.json",
        args.python_lock.resolve(),
        args.python_runtime_manifest.resolve(),
        args.python_runtime_archive.resolve(),
        args.python_stage_marker.resolve(),
    ]
    record: dict[str, Any] = {
        "schema": SCHEMA,
        "phase": "BEGIN",
        "created_utc": utc_now(),
        "workspace": str(workspace),
        "configuration": {
            "selected_rmw": args.rmw,
            "cyclonedds_shm": args.cyclonedds_shm,
            "dds_security": args.dds_security,
            "build_mdds": args.build_mdds,
        },
        "source_snapshot_sha256": source_sha,
        "source": source,
        "sdk": sdk,
        "inputs": {
            "files": [file_record(path, workspace) for path in input_paths],
            "patches": provenance.tree_digest(workspace / "patches"),
            "python": python_inputs,
        },
        "dependency_prefix_before_ros": provenance.tree_digest(install),
    }
    record["begin_payload_sha256"] = provenance.sha256_bytes(provenance.canonical_json(record))
    provenance.atomic_write(output, json.dumps(record, indent=2, sort_keys=True).encode() + b"\n")
    print(
        f"OHOS_BUILD_RECEIPT_BEGIN source_sha256={source_sha} "
        f"sdk_sha256={sdk['fingerprint_sha256']} "
        f"dependency_tree_sha256={record['dependency_prefix_before_ros']['sha256']}"
    )


def verify_begin_inputs(begin_record: dict[str, Any], workspace: Path, lock: Path, sdk_root: Path) -> None:
    _, current_source_sha = source_snapshot(workspace, lock)
    if current_source_sha != begin_record.get("source_snapshot_sha256"):
        raise ReceiptError("source snapshot changed during the ROS build")
    current_sdk = provenance.sdk_fingerprint(sdk_root)
    if current_sdk.get("fingerprint_sha256") != begin_record.get("sdk", {}).get("fingerprint_sha256"):
        raise ReceiptError("OHOS SDK changed during the ROS build")
    for expected in begin_record.get("inputs", {}).get("files", []):
        label = expected.get("path")
        path = Path(label)
        if not path.is_absolute():
            path = workspace / path
        if provenance.sha256_file(regular(path, "recorded build input")) != expected.get("sha256"):
            raise ReceiptError(f"build input changed during the ROS build: {label}")
    if provenance.tree_digest(workspace / "patches") != begin_record.get("inputs", {}).get("patches"):
        raise ReceiptError("patch tree changed during the ROS build")
    recorded_python = begin_record.get("inputs", {}).get("python", {})
    paths = recorded_python.get("paths", {})
    required = {"lock", "runtime_manifest", "runtime_archive", "stage_marker", "target_root"}
    if not isinstance(paths, dict) or set(paths) != required:
        raise ReceiptError("begin receipt lacks the complete Python input path set")
    current_python = validate_python_inputs(
        recorded_path(paths["runtime_manifest"], workspace),
        recorded_path(paths["runtime_archive"], workspace),
        recorded_path(paths["stage_marker"], workspace),
        recorded_path(paths["lock"], workspace),
        recorded_path(paths["target_root"], workspace),
    )
    for key in (
        "runtime_archive_sha256",
        "runtime_archive_bytes",
        "runtime_tree_sha256",
        "runtime_entry_count",
        "runtime_payload_bytes",
        "runtime_interface_sha256",
        "runtime_library_sha256",
        "provenance_mode",
        "stage_tree_sha256",
        "source_build_receipt",
        "source_lock_sha256",
        "source_build_recipe_sha256",
    ):
        if current_python.get(key) != recorded_python.get(key):
            raise ReceiptError(f"Python build input changed during the ROS build: {key}")


def finish(args: argparse.Namespace) -> None:
    begin_path = regular(args.begin, "begin receipt")
    begin_record = checked_json(begin_path, "begin receipt")
    if begin_record.get("schema") != SCHEMA or begin_record.get("phase") != "BEGIN":
        raise ReceiptError("begin receipt has the wrong schema or phase")
    workspace = args.workspace.resolve()
    lock = regular(args.lock if args.lock.is_absolute() else workspace / args.lock, "ROS lock")
    verify_begin_inputs(begin_record, workspace, lock, args.sdk_root)
    build_log = regular(args.build_log, "colcon events log")
    expected = read_expected_packages(args.expected_packages)
    text = build_log.read_text(encoding="utf-8", errors="replace")
    jobs: dict[str, int] = {}
    for context_name, identifier, rc_text in JOB_ENDED.findall(text):
        if context_name != identifier or identifier in jobs:
            raise ReceiptError(f"ambiguous JobEnded record for {identifier}")
        jobs[identifier] = int(rc_text)
    actual = sorted(jobs)
    if actual != expected:
        missing = sorted(set(expected) - set(actual))
        extra = sorted(set(actual) - set(expected))
        raise ReceiptError(f"colcon package completion set differs: missing={missing} extra={extra}")
    failed = {name: rc for name, rc in jobs.items() if rc != 0}
    if failed:
        raise ReceiptError(f"colcon recorded failed packages: {failed}")
    if "EventReactorShutdown" not in text:
        raise ReceiptError("colcon events log has no EventReactorShutdown terminal event")
    install = args.install_root.resolve()
    if not install.is_dir() or install.is_symlink():
        raise ReceiptError(f"final install prefix is missing: {install}")
    output = args.output.resolve()
    if output.exists() or output.is_symlink():
        raise ReceiptError(f"final receipt output is create-only: {output}")
    record: dict[str, Any] = dict(begin_record)
    record.update(
        {
            "phase": "COMPLETE",
            "completed_utc": utc_now(),
            "result": "PASS",
            "begin_receipt": file_record(begin_path, begin_path.parent),
            "colcon": {
                "events_log": file_record(build_log, workspace),
                "expected_packages_sha256": provenance.sha256_file(args.expected_packages.resolve()),
                "package_count": len(expected),
                "packages": expected,
                "all_job_return_codes_zero": True,
                "terminal_event": "EventReactorShutdown",
            },
            "install_tree_after_ros": provenance.tree_digest(install),
        }
    )
    record["complete_payload_sha256"] = provenance.sha256_bytes(provenance.canonical_json(record))
    provenance.atomic_write(output, json.dumps(record, indent=2, sort_keys=True).encode() + b"\n")
    print(
        f"OHOS_BUILD_RECEIPT_COMPLETE packages={len(expected)} "
        f"source_sha256={record['source_snapshot_sha256']} "
        f"install_tree_sha256={record['install_tree_after_ros']['sha256']}"
    )


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    sub = result.add_subparsers(dest="command", required=True)
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--workspace", type=Path, required=True)
    common.add_argument("--lock", type=Path, required=True)
    common.add_argument("--sdk-root", type=Path, required=True)
    start = sub.add_parser("begin", parents=[common])
    start.add_argument("--install-root", type=Path, required=True)
    start.add_argument("--rmw", required=True, choices=("rmw_fastrtps_cpp", "rmw_cyclonedds_cpp"))
    start.add_argument("--cyclonedds-shm", required=True, choices=("ON", "OFF"))
    start.add_argument("--dds-security", required=True, choices=("ON", "OFF"))
    start.add_argument("--build-mdds", required=True, choices=("ON", "OFF"))
    start.add_argument("--python-lock", type=Path, required=True)
    start.add_argument("--python-runtime-manifest", type=Path, required=True)
    start.add_argument("--python-runtime-archive", type=Path, required=True)
    start.add_argument("--python-stage-marker", type=Path, required=True)
    start.add_argument("--python-target-root", type=Path, required=True)
    start.add_argument("--output", type=Path, required=True)
    start.set_defaults(handler=begin)
    close = sub.add_parser("finish", parents=[common])
    close.add_argument("--begin", type=Path, required=True)
    close.add_argument("--install-root", type=Path, required=True)
    close.add_argument("--build-log", type=Path, required=True)
    close.add_argument("--expected-packages", type=Path, required=True)
    close.add_argument("--output", type=Path, required=True)
    close.set_defaults(handler=finish)
    return result


def main() -> int:
    args = parser().parse_args()
    try:
        args.handler(args)
    except (ReceiptError, RuntimeError, OSError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
