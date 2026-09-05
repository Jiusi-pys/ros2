#!/usr/bin/env python3
"""Create a canonical provenance envelope for one KaihongOS ROS 2 artifact.

The archive hash alone proves only which bytes were deployed.  This collector
also binds those bytes to the complete meta/imported worktree state, the lock
manifest, the patch set, the Pixi lock, and a small but stable OHOS SDK
fingerprint.  Runtime gates copy the resulting digest into their own records.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import re
import stat
import subprocess
import tempfile
from pathlib import Path
from typing import Any

import yaml


CHUNK_SIZE = 1024 * 1024
SCHEMA = "ros2-ohos-release-provenance-v1"
HEX_SHA256 = re.compile(r"[0-9a-f]{64}")
JOB_ENDED = re.compile(
    r"^\[[^]]+\] \(([^)]*)\) JobEnded: \{'identifier': '([^']+)', 'rc': (-?[0-9]+)\}$",
    re.MULTILINE,
)


def sha256_file(path: Path) -> str:
    metadata = path.lstat()
    if stat.S_ISLNK(metadata.st_mode) or not stat.S_ISREG(metadata.st_mode):
        raise RuntimeError(f"expected a non-symlink regular file: {path}")
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(CHUNK_SIZE), b""):
            digest.update(block)
    return digest.hexdigest()


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def canonical_json(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False) + "\n").encode(
        "utf-8"
    )


def run(argv: list[str], *, cwd: Path | None = None) -> bytes:
    completed = subprocess.run(
        argv,
        cwd=str(cwd) if cwd else None,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"command failed ({completed.returncode}): {' '.join(argv)}\n"
            f"{completed.stderr.decode('utf-8', 'replace')}"
        )
    return completed.stdout


def git(repo: Path, *args: str) -> bytes:
    return run(["git", "-C", str(repo), *args])


def object_id(repo: Path, expression: str) -> str:
    value = git(repo, "rev-parse", expression).decode("ascii", "strict").strip()
    if len(value) not in {40, 64} or any(character not in "0123456789abcdef" for character in value):
        raise RuntimeError(f"invalid Git object id for {repo}: {value!r}")
    return value


def untracked_inventory(repo: Path) -> list[dict[str, Any]]:
    inventory: list[dict[str, Any]] = []
    raw = git(repo, "ls-files", "--others", "--exclude-standard", "-z")
    for encoded in sorted(part for part in raw.split(b"\0") if part):
        relative = encoded.decode("utf-8", "surrogateescape")
        path = repo / relative
        metadata = path.lstat()
        if stat.S_ISLNK(metadata.st_mode) or not stat.S_ISREG(metadata.st_mode):
            raise RuntimeError(f"untracked source input is not a regular file: {path}")
        inventory.append(
            {
                "path": relative.replace("\\", "/"),
                "bytes": metadata.st_size,
                "sha256": sha256_file(path),
            }
        )
    return inventory


def repository_state(repo: Path, name: str) -> dict[str, Any]:
    if not (repo / ".git").exists():
        raise RuntimeError(f"missing Git worktree for {name}: {repo}")
    state: dict[str, Any] = {
        "name": name,
        "head": object_id(repo, "HEAD"),
        "head_tree": object_id(repo, "HEAD^{tree}"),
        "status_porcelain_v1_z_sha256": sha256_bytes(
            git(repo, "status", "--porcelain=v1", "-z", "-uall")
        ),
        "diff_head_binary_sha256": sha256_bytes(
            git(repo, "diff", "--binary", "--full-index", "--no-ext-diff", "HEAD")
        ),
        "diff_cached_binary_sha256": sha256_bytes(
            git(repo, "diff", "--cached", "--binary", "--full-index", "--no-ext-diff", "HEAD")
        ),
        "untracked": untracked_inventory(repo),
    }
    state["state_sha256"] = sha256_bytes(canonical_json(state))
    return state


def tree_digest(root: Path) -> dict[str, Any]:
    if not root.is_dir() or root.is_symlink():
        raise RuntimeError(f"expected a non-symlink directory: {root}")
    entries: list[dict[str, Any]] = []
    for path in sorted(root.rglob("*"), key=lambda item: item.relative_to(root).as_posix()):
        metadata = path.lstat()
        if stat.S_ISLNK(metadata.st_mode):
            raise RuntimeError(f"refusing symlink in provenance input tree: {path}")
        if stat.S_ISDIR(metadata.st_mode):
            continue
        if not stat.S_ISREG(metadata.st_mode):
            raise RuntimeError(f"refusing non-regular provenance input: {path}")
        entries.append(
            {
                "path": path.relative_to(root).as_posix(),
                "bytes": metadata.st_size,
                "sha256": sha256_file(path),
            }
        )
    return {"files": len(entries), "sha256": sha256_bytes(canonical_json(entries))}


def sdk_fingerprint(sdk_root: Path) -> dict[str, Any]:
    sdk_root = sdk_root.resolve()
    clang = sdk_root / "llvm" / "bin" / "clang.exe"
    libc = sdk_root / "sysroot" / "usr" / "lib" / "aarch64-linux-ohos" / "libc.so"
    package = sdk_root / "oh-uni-package.json"
    for path in (clang, libc, package):
        if not path.is_file() or path.is_symlink():
            raise RuntimeError(f"required OHOS SDK identity file is missing: {path}")
    version = run([str(clang), "--version"]).decode("utf-8", "replace").splitlines()
    identity = {
        "clang_version": version[0] if version else "UNKNOWN",
        "clang_sha256": sha256_file(clang),
        "target_libc_sha256": sha256_file(libc),
        "package_metadata_sha256": sha256_file(package),
    }
    # The installed path is useful audit context, but it is not part of the
    # SDK identity: identical SDK bytes at another workstation path must retain
    # the same fingerprint.
    result = {"root": str(sdk_root), **identity}
    result["fingerprint_sha256"] = sha256_bytes(canonical_json(identity))
    return result


def load_repository_names(lock: Path) -> list[str]:
    data = yaml.safe_load(lock.read_text(encoding="utf-8"))
    repositories = data.get("repositories") if isinstance(data, dict) else None
    if not isinstance(repositories, dict) or not repositories:
        raise RuntimeError(f"lock manifest has no repositories mapping: {lock}")
    names = list(repositories)
    if any(not isinstance(name, str) or not name for name in names):
        raise RuntimeError(f"lock manifest contains an invalid repository name: {lock}")
    return names


def atomic_write(path: Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def load_complete_build_receipt(path: Path, workspace: Path) -> dict[str, Any]:
    if not path.is_file() or path.is_symlink():
        raise RuntimeError(f"clean-build receipt is missing: {path}")
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise RuntimeError(f"invalid clean-build receipt: {path}: {exc}") from exc
    if not isinstance(value, dict) or value.get("schema") != "ros2-ohos-clean-build-receipt-v1":
        raise RuntimeError("clean-build receipt has the wrong schema")
    if value.get("phase") != "COMPLETE" or value.get("result") != "PASS":
        raise RuntimeError("clean-build receipt is not a completed PASS")

    complete_claim = value.get("complete_payload_sha256")
    complete_payload = dict(value)
    complete_payload.pop("complete_payload_sha256", None)
    if not isinstance(complete_claim, str) or not HEX_SHA256.fullmatch(complete_claim):
        raise RuntimeError("clean-build receipt lacks a valid complete payload digest")
    if sha256_bytes(canonical_json(complete_payload)) != complete_claim:
        raise RuntimeError("clean-build receipt complete payload digest mismatch")

    begin_claim = value.get("begin_payload_sha256")
    begin_payload = dict(value)
    for field in (
        "begin_payload_sha256",
        "completed_utc",
        "result",
        "begin_receipt",
        "colcon",
        "install_tree_after_ros",
        "complete_payload_sha256",
    ):
        begin_payload.pop(field, None)
    begin_payload["phase"] = "BEGIN"
    if not isinstance(begin_claim, str) or not HEX_SHA256.fullmatch(begin_claim):
        raise RuntimeError("clean-build receipt lacks a valid begin payload digest")
    if sha256_bytes(canonical_json(begin_payload)) != begin_claim:
        raise RuntimeError("clean-build receipt begin payload digest mismatch")

    reconstructed_begin = dict(begin_payload)
    reconstructed_begin["begin_payload_sha256"] = begin_claim
    reconstructed_begin_bytes = (
        json.dumps(reconstructed_begin, indent=2, sort_keys=True).encode("utf-8") + b"\n"
    )
    begin_file = value.get("begin_receipt")
    if (
        not isinstance(begin_file, dict)
        or begin_file.get("bytes") != len(reconstructed_begin_bytes)
        or begin_file.get("sha256") != sha256_bytes(reconstructed_begin_bytes)
    ):
        raise RuntimeError("clean-build receipt does not contain its exact BEGIN receipt identity")

    colcon = value.get("colcon")
    packages = colcon.get("packages") if isinstance(colcon, dict) else None
    if (
        not isinstance(packages, list)
        or not packages
        or packages != sorted(set(packages))
        or any(not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9_.+-]+", name) for name in packages)
        or colcon.get("package_count") != len(packages)
        or colcon.get("all_job_return_codes_zero") is not True
        or colcon.get("terminal_event") != "EventReactorShutdown"
    ):
        raise RuntimeError("clean-build receipt has an invalid colcon completion contract")
    expected_packages_bytes = ("\n".join(packages) + "\n").encode("utf-8")
    if colcon.get("expected_packages_sha256") != sha256_bytes(expected_packages_bytes):
        raise RuntimeError("clean-build receipt package-list digest mismatch")

    event_record = colcon.get("events_log")
    event_label = event_record.get("path") if isinstance(event_record, dict) else None
    if not isinstance(event_label, str) or not event_label or Path(event_label).is_absolute():
        raise RuntimeError("clean-build receipt events log path must be workspace-relative")
    event_path = (workspace / event_label).resolve()
    try:
        event_path.relative_to(workspace)
    except ValueError as exc:
        raise RuntimeError("clean-build receipt events log escapes the workspace") from exc
    if (
        event_record.get("bytes") != event_path.stat().st_size
        or event_record.get("sha256") != sha256_file(event_path)
    ):
        raise RuntimeError("clean-build receipt events log bytes changed after the build")
    events = event_path.read_text(encoding="utf-8", errors="replace")
    jobs: dict[str, int] = {}
    for context_name, identifier, rc_text in JOB_ENDED.findall(events):
        if context_name != identifier or identifier in jobs:
            raise RuntimeError(f"ambiguous JobEnded event in retained build log: {identifier}")
        jobs[identifier] = int(rc_text)
    if sorted(jobs) != packages or any(return_code != 0 for return_code in jobs.values()):
        raise RuntimeError("retained colcon events log does not prove the recorded successful package set")
    if "EventReactorShutdown" not in events:
        raise RuntimeError("retained colcon events log lacks its terminal event")
    return value


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--workspace", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--lock", type=Path, default=Path("ros2.ohos.lock.repos"))
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--install-manifest", type=Path, required=True)
    parser.add_argument("--install-root", type=Path, required=True)
    parser.add_argument("--sdk-root", type=Path, required=True)
    parser.add_argument("--rmw", required=True)
    parser.add_argument("--build-receipt", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    if not args.rmw or any(character not in "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_" for character in args.rmw):
        parser.error("--rmw must be one safe implementation identifier")

    workspace = args.workspace.resolve()
    lock = args.lock if args.lock.is_absolute() else workspace / args.lock
    archive = args.archive.resolve()
    install_manifest = args.install_manifest.resolve()
    install_root = args.install_root.resolve()
    build_receipt_path = args.build_receipt.resolve()
    output = args.output.resolve()
    for path in (lock, archive, install_manifest, workspace / "pixi.lock", workspace / "scripts" / "build_ohos.sh", workspace / "cmake" / "ohos-aarch64.toolchain.cmake"):
        if not path.is_file() or path.is_symlink():
            raise RuntimeError(f"required provenance input is missing: {path}")

    repository_names = load_repository_names(lock)
    repositories = [repository_state(workspace, "ros2-meta")]
    repositories.extend(
        repository_state(workspace / "src" / name, name) for name in repository_names
    )
    # A source tree changing while the sequential collector runs must not be
    # described as one coherent release input.  Re-read every repository and
    # compare its complete state digest before writing the record.
    for captured in repositories:
        repo = workspace if captured["name"] == "ros2-meta" else workspace / "src" / captured["name"]
        current = repository_state(repo, captured["name"])
        if current["state_sha256"] != captured["state_sha256"]:
            raise RuntimeError(f"source repository changed during provenance collection: {captured['name']}")
    source_payload = {
        "meta": repositories[0],
        "repositories": repositories[1:],
    }
    source_snapshot_sha256 = sha256_bytes(canonical_json(source_payload))

    sdk = sdk_fingerprint(args.sdk_root)
    build_receipt = load_complete_build_receipt(build_receipt_path, workspace)
    if build_receipt.get("inputs", {}).get("python", {}).get("provenance_mode") != "source-reproducible":
        raise RuntimeError("generic release requires source-built Python, not an artifact-only runtime")
    if build_receipt.get("source_snapshot_sha256") != source_snapshot_sha256:
        raise RuntimeError("clean-build receipt belongs to a different source snapshot")
    if build_receipt.get("sdk", {}).get("fingerprint_sha256") != sdk["fingerprint_sha256"]:
        raise RuntimeError("clean-build receipt belongs to a different OHOS SDK")
    if build_receipt.get("configuration", {}).get("selected_rmw") != args.rmw:
        raise RuntimeError("clean-build receipt selected a different default RMW")
    current_install_tree = tree_digest(install_root)
    if build_receipt.get("install_tree_after_ros") != current_install_tree:
        raise RuntimeError("install tree differs from the completed clean-build receipt")

    inputs: dict[str, Any] = {
        "lock_manifest_sha256": sha256_file(lock),
        "patches": tree_digest(workspace / "patches"),
        "pixi_lock_sha256": sha256_file(workspace / "pixi.lock"),
        "build_script_sha256": sha256_file(workspace / "scripts" / "build_ohos.sh"),
        "toolchain_sha256": sha256_file(workspace / "cmake" / "ohos-aarch64.toolchain.cmake"),
        "clean_build_receipt": {
            "path": str(build_receipt_path),
            "bytes": build_receipt_path.stat().st_size,
            "sha256": sha256_file(build_receipt_path),
            "complete_payload_sha256": build_receipt.get("complete_payload_sha256"),
            "package_count": build_receipt.get("colcon", {}).get("package_count"),
            "events_log_sha256": build_receipt.get("colcon", {}).get("events_log", {}).get("sha256"),
            "install_tree_sha256": current_install_tree["sha256"],
        },
        "python": build_receipt.get("inputs", {}).get("python"),
    }

    record: dict[str, Any] = {
        "schema": SCHEMA,
        "created_utc": dt.datetime.now(dt.timezone.utc).isoformat().replace("+00:00", "Z"),
        "target": "aarch64-linux-ohos",
        "selected_rmw": args.rmw,
        "source_snapshot_sha256": source_snapshot_sha256,
        "source": source_payload,
        "inputs": inputs,
        "sdk": sdk,
        "artifact": {
            "archive_name": archive.name,
            "archive_bytes": archive.stat().st_size,
            "archive_sha256": sha256_file(archive),
            "install_manifest_sha256": sha256_file(install_manifest),
        },
    }
    record["record_sha256"] = sha256_bytes(canonical_json(record))
    atomic_write(output, json.dumps(record, ensure_ascii=False, indent=2, sort_keys=True).encode("utf-8") + b"\n")
    print(
        "PROVENANCE"
        f" source_snapshot_sha256={source_snapshot_sha256}"
        f" record_sha256={record['record_sha256']}"
        f" archive_sha256={record['artifact']['archive_sha256']}"
        f" repositories={len(repository_names)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
