#!/usr/bin/env python3
"""Freeze a hash-sealed verification evidence bundle.

This deliberately does *not* call the result immutable: a local directory and
SHA-256 checksums are tamper-evident only after their digest is retained by an
independent system (for example a signed release record or WORM store).

The collector requires exactly the ros2, mdds, and rmw_mdds worktrees; the
three delivery artifacts with their expected basenames; named script/config
inputs; the raw-ANNOUNCE and per-gate evidence inputs required by this release
contract; seven named raw logs; seven named command logs; and the gateway
profile whose bytes are deployed.  It records worktrees exactly as they stood
at collection time, including binary diffs and hashes for every non-ignored
untracked file.  All copied inputs and archive contents are symlink-free so
the resulting bundle is self-contained.
Output is outside the ROS workspace by design, so the evidence directory
cannot add a self-referential untracked entry.

``--out`` is create-only and may not be inside a directory that already
contains ``manifest.v1.json``.  This prevents a later collection from being
mistaken for an addition to an immutable prior bundle.  Each repository state
is captured and then re-read sequentially before this script writes
``manifest.v1.json``; a changed HEAD, status, diff, or untracked inventory
aborts without creating a seemingly valid manifest.  The collector does not
hold a cross-repository write lock, so these per-repository comparisons are
not an atomic snapshot of all three worktrees at one instant.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import platform
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path
from typing import Any, Iterable


CHUNK = 1024 * 1024
REQUIRED_REPOSITORIES = frozenset({"ros2", "mdds", "rmw_mdds"})
REQUIRED_ARTIFACTS = frozenset({"libmdds", "librmw_mdds", "mdds_gateway"})
REQUIRED_ARTIFACT_BASENAMES = {
    "libmdds": "libmdds.so",
    "librmw_mdds": "librmw_mdds.so",
    "mdds_gateway": "mdds_gateway",
}
REQUIRED_GATEWAY_PROFILE_BASENAME = "mdds_gateway_ohos_dsoftbus.conf"
# Evidence required for every release candidate in this verification contract.
# The collector permits additional named inputs so a caller can retain
# run-specific material without weakening this minimum.  In particular,
# ``domain0_smoke`` is optional because it is required only for a release that
# declares that production-domain smoke was executed.
REQUIRED_INPUT_NAMES = frozenset({
    "script_config",
    "run_contract",
    "raw_announce",
    "board_a_evidence",
    "board_b_evidence",
    "ds_evidence",
    "gw_evidence",
    "rmw_evidence",
    "deploy_evidence",
})
OPTIONAL_EXECUTED_INPUT_NAMES = frozenset({"domain0_smoke"})
REQUIRED_RAW_LOG_NAMES = frozenset({"build", "deploy", "board_a", "board_b", "ds", "gw", "rmw"})
REQUIRED_COMMAND_LOG_NAMES = frozenset({"build", "deploy", "board_a", "board_b", "ds", "gw", "rmw"})
GIT_SNAPSHOT_COMMANDS: dict[str, list[str]] = {
    "head": ["git", "rev-parse", "HEAD"],
    "head_detail": ["git", "show", "-s", "--format=fuller", "HEAD"],
    "status_porcelain_z": ["git", "status", "--porcelain=v1", "-z", "-uall"],
    "diff_check": ["git", "diff", "--check"],
    "diff_head_binary": ["git", "diff", "--binary", "--full-index", "--no-ext-diff", "HEAD"],
    "diff_cached_binary": ["git", "diff", "--cached", "--binary", "--full-index", "--no-ext-diff", "HEAD"],
    "diff_unstaged_binary": ["git", "diff", "--binary", "--full-index", "--no-ext-diff"],
    "untracked_z": ["git", "ls-files", "--others", "--exclude-standard", "-z"],
}
GIT_SNAPSHOT_FILENAMES = {
    "head": "HEAD.txt",
    "head_detail": "HEAD-detail.txt",
    "status_porcelain_z": "status.porcelain-v1.z",
    "diff_check": "diff.check.txt",
    "diff_head_binary": "worktree-vs-HEAD.binary.patch",
    "diff_cached_binary": "staged-vs-HEAD.binary.patch",
    "diff_unstaged_binary": "unstaged-vs-index.binary.patch",
    "untracked_z": "untracked.z",
}
GIT_STABILITY_COMMAND_KEYS = (
    "head",
    "status_porcelain_z",
    "diff_head_binary",
    "diff_cached_binary",
    "diff_unstaged_binary",
    "untracked_z",
)


def is_symlink_or_reparse_point(metadata: os.stat_result) -> bool:
    """Treat Windows junctions/reparse points as links as well as POSIX links."""
    reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)
    attributes = getattr(metadata, "st_file_attributes", 0)
    return stat.S_ISLNK(metadata.st_mode) or bool(reparse_flag and attributes & reparse_flag)


def exists_or_link(path: Path) -> bool:
    """Like exists(), but a broken symlink/reparse point still counts as occupied."""
    try:
        metadata = path.lstat()
    except FileNotFoundError:
        return False
    return True


def sha256_file(path: Path) -> str:
    metadata = path.lstat()
    if is_symlink_or_reparse_point(metadata) or not stat.S_ISREG(metadata.st_mode):
        raise RuntimeError(f"refusing to hash non-regular file: {path}")
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(CHUNK), b""):
            digest.update(block)
    return digest.hexdigest()


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def run(argv: list[str], cwd: Path) -> bytes:
    completed = subprocess.run(
        argv,
        cwd=str(cwd),
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if completed.returncode != 0:
        command = " ".join(argv)
        raise RuntimeError(
            f"command failed ({completed.returncode}): {command}\n"
            f"{completed.stderr.decode('utf-8', 'replace')}"
        )
    return completed.stdout


def parse_assignment(value: str, label: str) -> tuple[str, str]:
    if "=" not in value:
        raise argparse.ArgumentTypeError(f"{label} must be NAME=PATH: {value}")
    name, path = value.split("=", 1)
    if not name or not path:
        raise argparse.ArgumentTypeError(f"{label} must be NAME=PATH: {value}")
    try:
        safe_name(name)
    except ValueError as error:
        raise argparse.ArgumentTypeError(str(error)) from error
    return name, path


def safe_name(value: str) -> str:
    if not value or value in {".", ".."} or any(c not in "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-" for c in value):
        raise ValueError(f"unsafe evidence name: {value!r}")
    return value


def lexical_absolute(raw_path: str | Path) -> Path:
    """Return an absolute lexical path without silently resolving symlinks."""
    path = Path(raw_path).expanduser()
    if not path.is_absolute():
        path = Path.cwd() / path
    return Path(os.path.abspath(path))


def canonical_lexical_key(path: str | Path) -> str:
    """Return a case-normalized absolute lexical key without link resolution."""
    return os.path.normcase(os.path.normpath(str(lexical_absolute(path))))


def lexical_contains(parent: Path, child: Path) -> bool:
    """Whether ``child`` is lexically equal to or nested below ``parent``."""
    parent_key = canonical_lexical_key(parent)
    child_key = canonical_lexical_key(child)
    try:
        return os.path.commonpath([parent_key, child_key]) == parent_key
    except ValueError:
        # Different Windows drives cannot contain one another.
        return False


def assert_no_symlink_components(path: Path, label: str) -> None:
    """Reject a symlink anywhere in an existing lexical path prefix."""
    absolute = lexical_absolute(path)
    parts = absolute.parts
    current = Path(absolute.anchor) if absolute.anchor else Path()
    start = 1 if absolute.anchor else 0
    for part in parts[start:]:
        current /= part
        try:
            metadata = current.lstat()
        except FileNotFoundError:
            continue
        if is_symlink_or_reparse_point(metadata):
            raise RuntimeError(f"{label} traverses a symlink or reparse point: {current}")


def classify_non_symlink_path(path: Path, label: str) -> str:
    assert_no_symlink_components(path, label)
    try:
        metadata = path.lstat()
    except FileNotFoundError as error:
        raise FileNotFoundError(path) from error
    if is_symlink_or_reparse_point(metadata):
        raise RuntimeError(f"{label} must not be a symlink or reparse point: {path}")
    if stat.S_ISREG(metadata.st_mode):
        return "file"
    if stat.S_ISDIR(metadata.st_mode):
        return "directory"
    raise RuntimeError(f"{label} must be a regular file or directory: {path}")


def assert_tree_has_no_symlinks(root: Path, label: str) -> None:
    """Reject symlinks and special files in a copied/archive input tree."""
    root_kind = classify_non_symlink_path(root, label)
    if root_kind == "file":
        return
    for directory, names, files in os.walk(root, topdown=True, followlinks=False):
        current = Path(directory)
        for name in sorted([*names, *files]):
            candidate = current / name
            try:
                metadata = candidate.lstat()
            except FileNotFoundError as error:
                raise RuntimeError(f"{label} changed during scan: {candidate}") from error
            if is_symlink_or_reparse_point(metadata):
                raise RuntimeError(f"{label} contains a symlink or reparse point: {candidate}")
            if not (stat.S_ISREG(metadata.st_mode) or stat.S_ISDIR(metadata.st_mode)):
                raise RuntimeError(f"{label} contains a non-regular entry: {candidate}")


def directory_sha256(root: Path) -> str:
    """Hash a symlink-free tree deterministically, including relative names."""
    digest = hashlib.sha256()
    for path in sorted(root.rglob("*"), key=lambda item: item.as_posix()):
        metadata = path.lstat()
        if is_symlink_or_reparse_point(metadata):
            raise RuntimeError(f"refusing to hash symlinked tree entry: {path}")
        relative = path.relative_to(root).as_posix().encode("utf-8", "surrogateescape")
        if stat.S_ISDIR(metadata.st_mode):
            digest.update(b"D\0" + relative + b"\0")
        elif stat.S_ISREG(metadata.st_mode):
            digest.update(b"F\0" + relative + b"\0")
            digest.update(sha256_file(path).encode("ascii") + b"\0")
        else:
            raise RuntimeError(f"refusing to hash non-regular tree entry: {path}")
    return digest.hexdigest()


def copy_input(
    source: Path,
    destination_root: Path,
    *,
    label: str,
    require_regular_file: bool = False,
) -> dict[str, Any]:
    source = lexical_absolute(source)
    kind = classify_non_symlink_path(source, label)
    if require_regular_file and kind != "file":
        raise RuntimeError(f"{label} must be a non-symlink regular file: {source}")
    assert_tree_has_no_symlinks(source, label)
    destination_root.mkdir(parents=True, exist_ok=True)
    assert_no_symlink_components(destination_root, f"{label} destination")
    destination = destination_root / source.name
    if exists_or_link(destination):
        raise FileExistsError(f"{label} destination already exists: {destination}")
    if kind == "directory":
        source_digest = directory_sha256(source)
        shutil.copytree(source, destination, symlinks=False)
        assert_tree_has_no_symlinks(destination, f"{label} copied tree")
        destination_digest = directory_sha256(destination)
    else:
        source_digest = sha256_file(source)
        shutil.copy2(source, destination)
        if is_symlink_or_reparse_point(destination.lstat()) or not destination.is_file():
            raise RuntimeError(f"{label} copy is not a regular file: {destination}")
        destination_digest = sha256_file(destination)
    if source_digest != destination_digest:
        raise RuntimeError(
            f"{label} copy hash mismatch: source={source_digest} destination={destination_digest}")
    return {
        "source": str(source),
        "copied": str(destination),
        "kind": kind,
        "sha256": destination_digest,
    }


def write_bytes(path: Path, content: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    assert_no_symlink_components(path.parent, "evidence output")
    if exists_or_link(path):
        raise FileExistsError(f"refusing to overwrite evidence file: {path}")
    with path.open("xb") as stream:
        stream.write(content)


def json_dump(path: Path, value: Any) -> None:
    write_bytes(
        path,
        (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8"),
    )


def collect_untracked_inventory(repo: Path, untracked_z: bytes) -> list[dict[str, Any]]:
    """Hash every non-ignored untracked entry as part of repository state."""
    untracked: list[dict[str, Any]] = []
    for raw_path in untracked_z.split(b"\0"):
        if not raw_path:
            continue
        relative = raw_path.decode("utf-8", "surrogateescape")
        candidate = repo / relative
        try:
            metadata = candidate.lstat()
        except FileNotFoundError:
            untracked.append({"path": relative, "state": "vanished-during-collection"})
            continue
        item: dict[str, Any] = {"path": relative, "mode": stat.S_IFMT(metadata.st_mode), "size": metadata.st_size}
        if stat.S_ISREG(metadata.st_mode):
            item["sha256"] = sha256_file(candidate)
            item["state"] = "file"
        elif stat.S_ISLNK(metadata.st_mode):
            target = os.readlink(candidate)
            item["sha256"] = sha256_bytes(target.encode("utf-8", "surrogateescape"))
            item["state"] = "symlink"
            item["target"] = target
        else:
            item["state"] = "non-regular"
        untracked.append(item)
    return untracked


def capture_git_state(repo: Path) -> tuple[dict[str, bytes], list[dict[str, Any]]]:
    """Read all Git state that is sealed into a repository snapshot."""
    if not (repo / ".git").exists():
        raise RuntimeError(f"not a Git worktree: {repo}")
    outputs = {
        key: run(command, repo)
        for key, command in GIT_SNAPSHOT_COMMANDS.items()
    }
    return outputs, collect_untracked_inventory(repo, outputs["untracked_z"])


def changed_git_state_fields(
    before: tuple[dict[str, bytes], list[dict[str, Any]]],
    after: tuple[dict[str, bytes], list[dict[str, Any]]],
) -> list[str]:
    """Return every sealed repository-state field that changed during collection."""
    before_outputs, before_untracked = before
    after_outputs, after_untracked = after
    changed = [
        key
        for key in GIT_STABILITY_COMMAND_KEYS
        if before_outputs[key] != after_outputs[key]
    ]
    if before_untracked != after_untracked:
        changed.append("untracked_inventory")
    return changed


def assert_git_state_stable(
    name: str,
    before: tuple[dict[str, bytes], list[dict[str, Any]]],
    after: tuple[dict[str, bytes], list[dict[str, Any]]],
) -> None:
    changed = changed_git_state_fields(before, after)
    if changed:
        raise RuntimeError(
            "repository changed during evidence collection: "
            f"{name} ({', '.join(changed)})")


def write_git_snapshot(
    name: str,
    repo: Path,
    out: Path,
    state: tuple[dict[str, bytes], list[dict[str, Any]]],
) -> dict[str, Any]:
    """Write a previously captured state; callers perform the final re-check."""
    outputs, untracked = state
    repo_out = out / "source" / safe_name(name)
    repo_out.mkdir(parents=True, exist_ok=False)
    for key, filename in GIT_SNAPSHOT_FILENAMES.items():
        write_bytes(repo_out / filename, outputs[key])
    json_dump(repo_out / "untracked.sha256.json", untracked)

    return {
        "name": name,
        "path": str(repo),
        "head": outputs["head"].decode("ascii", "strict").strip(),
        "head_detail_sha256": sha256_bytes(outputs["head_detail"]),
        "status_sha256": sha256_bytes(outputs["status_porcelain_z"]),
        "diff_head_sha256": sha256_bytes(outputs["diff_head_binary"]),
        "diff_cached_sha256": sha256_bytes(outputs["diff_cached_binary"]),
        "diff_unstaged_sha256": sha256_bytes(outputs["diff_unstaged_binary"]),
        "diff_check_sha256": sha256_bytes(outputs["diff_check"]),
        "untracked_count": len(untracked),
        "untracked_inventory_sha256": sha256_file(repo_out / "untracked.sha256.json"),
    }


def git_snapshot(name: str, repo: Path, out: Path) -> dict[str, Any]:
    """Compatibility helper for callers that only need to serialize one state."""
    return write_git_snapshot(name, repo, out, capture_git_state(repo))


def iter_regular_files(root: Path, excluded: set[Path] | None = None) -> Iterable[Path]:
    excluded = excluded or set()
    assert_tree_has_no_symlinks(root, "evidence output")
    for path in sorted(root.rglob("*"), key=lambda item: item.as_posix()):
        if path in excluded:
            continue
        metadata = path.lstat()
        if stat.S_ISREG(metadata.st_mode):
            yield path
        elif stat.S_ISDIR(metadata.st_mode):
            continue
        else:
            raise RuntimeError(f"evidence output contains a non-regular entry: {path}")


def sha256_entry(path: Path) -> str:
    if is_symlink_or_reparse_point(path.lstat()):
        raise RuntimeError(f"refusing to hash symlinked output entry: {path}")
    return sha256_file(path)


def write_integrity_index(out: Path) -> tuple[Path, str]:
    integrity = out / "integrity"
    if exists_or_link(integrity):
        raise FileExistsError(f"integrity destination already exists: {integrity}")
    integrity.mkdir()
    sums = integrity / "SHA256SUMS"
    manifest_hash = integrity / "manifest.v1.json.sha256"
    excluded = {sums, manifest_hash}
    rows: list[str] = []
    for path in iter_regular_files(out, excluded):
        relative = path.relative_to(out).as_posix()
        rows.append(f"{sha256_entry(path)}  file  {relative}")
    write_bytes(sums, ("\n".join(rows) + "\n").encode("utf-8"))
    manifest = out / "manifest.v1.json"
    manifest_digest = sha256_file(manifest)
    write_bytes(
        manifest_hash,
        f"{manifest_digest}  file  manifest.v1.json\n".encode("utf-8"),
    )
    return sums, manifest_digest


def write_deterministic_tar(out: Path) -> tuple[Path, str]:
    archive = out.parent / f"{out.name}.tar"
    if exists_or_link(archive):
        raise FileExistsError(f"refusing to overwrite evidence archive: {archive}")
    assert_no_symlink_components(archive.parent, "archive destination")
    temporary_handle, temporary_name = tempfile.mkstemp(
        prefix=f".{out.name}.", suffix=".tar.tmp", dir=archive.parent)
    os.close(temporary_handle)
    temporary = Path(temporary_name)
    try:
        with tarfile.open(temporary, "w") as tar:
            for path in iter_regular_files(out):
                relative = path.relative_to(out).as_posix()
                info = tar.gettarinfo(str(path), arcname=relative)
                info.uid = 0
                info.gid = 0
                info.uname = ""
                info.gname = ""
                info.mtime = 0
                with path.open("rb") as stream:
                    tar.addfile(info, stream)
        # link() is an atomic create-only publish in the same parent directory;
        # unlike os.replace(), it cannot silently overwrite a sibling archive.
        os.link(temporary, archive)
    except Exception:
        temporary.unlink(missing_ok=True)
        raise
    temporary.unlink()
    digest = sha256_file(archive)
    write_bytes(
        out / "integrity" / "archive.sha256",
        f"{digest}  file  ../{archive.name}\n".encode("utf-8"),
    )
    return archive, digest


def reject_duplicate_assignment_names(
    assignments: list[tuple[str, str]],
    label: str,
) -> None:
    names = [name for name, _ in assignments]
    duplicates = sorted({name for name in names if names.count(name) > 1})
    if duplicates:
        raise ValueError(f"{label} contains duplicate names: {', '.join(duplicates)}")


def require_exact_assignment_names(
    assignments: list[tuple[str, str]],
    required: frozenset[str],
    label: str,
) -> None:
    reject_duplicate_assignment_names(assignments, label)
    actual = {name for name, _ in assignments}
    if actual != required:
        missing = sorted(required - actual)
        extra = sorted(actual - required)
        details: list[str] = []
        if missing:
            details.append(f"missing={','.join(missing)}")
        if extra:
            details.append(f"unexpected={','.join(extra)}")
        raise ValueError(
            f"{label} must name exactly {','.join(sorted(required))}"
            + (f" ({'; '.join(details)})" if details else ""))


def require_assignment_names_at_least(
    assignments: list[tuple[str, str]],
    required: frozenset[str],
    label: str,
) -> None:
    reject_duplicate_assignment_names(assignments, label)
    actual = {name for name, _ in assignments}
    missing = sorted(required - actual)
    if missing:
        raise ValueError(
            f"{label} must include {','.join(sorted(required))} "
            f"(missing={','.join(missing)})")


def reject_duplicate_canonical_paths(
    paths: Iterable[tuple[str, Path]],
    label: str,
) -> None:
    """Reject exact lexical aliases before any output directory is created."""
    seen: dict[str, list[str]] = {}
    for name, path in paths:
        key = canonical_lexical_key(path)
        seen.setdefault(key, []).append(name)
    duplicates = [(key, names) for key, names in seen.items() if len(names) > 1]
    if duplicates:
        details = "; ".join(
            f"{key} ({','.join(names)})" for key, names in sorted(duplicates))
        raise ValueError(f"{label} contains duplicate canonical paths: {details}")


def validate_evidence_assignments(
    assignments: list[tuple[str, str]],
    *,
    label: str,
    required_names: frozenset[str],
    require_exact_names: bool,
    require_regular_file: bool,
    output_path: Path | None = None,
) -> list[tuple[str, Path, str]]:
    if require_exact_names:
        require_exact_assignment_names(assignments, required_names, label)
    else:
        require_assignment_names_at_least(assignments, required_names, label)

    validated: list[tuple[str, Path, str]] = []
    for name, raw_path in assignments:
        path = lexical_absolute(raw_path)
        kind = classify_non_symlink_path(path, f"{label} {name}")
        if label == "--input" and kind == "directory" and output_path is not None:
            if lexical_contains(path, output_path) or lexical_contains(output_path, path):
                raise RuntimeError(
                    f"--input {name} directory overlaps evidence output: "
                    f"input={path} out={output_path}")
        if require_regular_file and kind != "file":
            raise RuntimeError(f"{label} {name} must be a non-symlink regular file: {path}")
        assert_tree_has_no_symlinks(path, f"{label} {name}")
        validated.append((name, path, kind))
    reject_duplicate_canonical_paths(
        ((name, path) for name, path, _ in validated), label)
    return validated


def reject_duplicate_evidence_paths(
    inputs: Iterable[tuple[str, Path, str]],
    raw_logs: Iterable[tuple[str, Path, str]],
    command_logs: Iterable[tuple[str, Path, str]],
) -> None:
    paths: list[tuple[str, Path]] = []
    for category, assignments in (
        ("--input", inputs),
        ("--raw-log", raw_logs),
        ("--command-log", command_logs),
    ):
        paths.extend((f"{category} {name}", path) for name, path, _ in assignments)
    reject_duplicate_canonical_paths(paths, "evidence assignments")


def validate_delivery_contract(args: argparse.Namespace) -> None:
    try:
        safe_name(args.run_id)
    except ValueError as error:
        raise ValueError("--run-id must be a safe evidence name") from error
    reject_duplicate_assignment_names(args.note, "--note")


def validated_repo_path(raw_path: str) -> Path:
    path = lexical_absolute(raw_path)
    if classify_non_symlink_path(path, "--repo") != "directory":
        raise RuntimeError(f"--repo must name a non-symlink directory: {path}")
    assert_no_symlink_components(path / ".git", "--repo")
    return path


def validate_repository_assignments(
    assignments: list[tuple[str, str]],
) -> dict[str, Path]:
    validated = [(name, validated_repo_path(raw_path)) for name, raw_path in assignments]
    reject_duplicate_canonical_paths(validated, "--repo")
    require_exact_assignment_names(assignments, REQUIRED_REPOSITORIES, "--repo")
    return dict(validated)


def validate_artifact_assignments(
    assignments: list[tuple[str, str]],
) -> list[tuple[str, Path]]:
    validated: list[tuple[str, Path]] = []
    for name, raw_path in assignments:
        path = lexical_absolute(raw_path)
        expected_basename = REQUIRED_ARTIFACT_BASENAMES.get(name)
        if expected_basename is None:
            raise ValueError(f"--artifact has unexpected name: {name}")
        if path.name != expected_basename:
            raise ValueError(
                f"--artifact {name} must name {expected_basename}: {path}")
        if classify_non_symlink_path(path, f"--artifact {name}") != "file":
            raise RuntimeError(f"--artifact {name} must be a non-symlink regular file: {path}")
        assert_tree_has_no_symlinks(path, f"--artifact {name}")
        validated.append((name, path))
    reject_duplicate_canonical_paths(validated, "--artifact")
    require_exact_assignment_names(assignments, REQUIRED_ARTIFACTS, "--artifact")
    return validated


def validate_gateway_profile(raw_path: str) -> Path:
    path = lexical_absolute(raw_path)
    if path.name != REQUIRED_GATEWAY_PROFILE_BASENAME:
        raise ValueError(
            "--gateway-profile must name "
            f"{REQUIRED_GATEWAY_PROFILE_BASENAME}: {path}")
    if classify_non_symlink_path(path, "--gateway-profile") != "file":
        raise RuntimeError(f"--gateway-profile must be a non-symlink regular file: {path}")
    assert_tree_has_no_symlinks(path, "--gateway-profile")
    return path


def existing_manifest_ancestor(path: Path) -> Path | None:
    """Return an existing ancestor that carries a prior manifest, if any.

    Treat any regular ``manifest.v1.json`` as a sealed-bundle boundary rather
    than attempting to infer whether a caller considers it immutable.  This is
    deliberately fail-closed: a new collection must be a sibling of an
    existing bundle, never a child of it.
    """
    current = lexical_absolute(path)
    while True:
        manifest = current / "manifest.v1.json"
        if exists_or_link(manifest):
            if classify_non_symlink_path(manifest, "existing verification manifest") != "file":
                raise RuntimeError(
                    "existing verification manifest must be a non-symlink regular file: "
                    f"{manifest}")
            return current
        parent = current.parent
        if parent == current:
            return None
        current = parent


def prepare_output_path(raw_path: str) -> Path:
    out = lexical_absolute(raw_path)
    assert_no_symlink_components(out, "evidence output")
    protected_ancestor = existing_manifest_ancestor(out)
    if protected_ancestor is not None:
        raise RuntimeError(
            "evidence output must not be inside an existing verification manifest bundle: "
            f"{protected_ancestor}")
    if exists_or_link(out):
        raise RuntimeError(f"refusing to overwrite evidence directory: {out}")
    archive = out.parent / f"{out.name}.tar"
    if exists_or_link(archive):
        raise RuntimeError(f"refusing to overwrite evidence archive: {archive}")
    return out


def ensure_output_outside_repositories(out: Path, repositories: Iterable[Path]) -> None:
    for repository in repositories:
        if lexical_contains(repository, out):
            raise RuntimeError(f"evidence output must be outside repository: {repository}")


def validate_collection_plan(args: argparse.Namespace) -> tuple[
    dict[str, Path],
    list[tuple[str, Path, str]],
    list[tuple[str, Path, str]],
    list[tuple[str, Path, str]],
    list[tuple[str, Path]],
    Path,
    Path,
]:
    """Validate every source and destination boundary before creating ``--out``."""
    validate_delivery_contract(args)
    repository_paths = validate_repository_assignments(args.repo)
    out = prepare_output_path(args.out)
    ensure_output_outside_repositories(out, repository_paths.values())
    inputs = validate_evidence_assignments(
        args.input,
        label="--input",
        required_names=REQUIRED_INPUT_NAMES,
        require_exact_names=False,
        require_regular_file=False,
        output_path=out,
    )
    raw_logs = validate_evidence_assignments(
        args.raw_log,
        label="--raw-log",
        required_names=REQUIRED_RAW_LOG_NAMES,
        require_exact_names=True,
        require_regular_file=True,
    )
    command_logs = validate_evidence_assignments(
        args.command_log,
        label="--command-log",
        required_names=REQUIRED_COMMAND_LOG_NAMES,
        require_exact_names=False,
        require_regular_file=True,
    )
    reject_duplicate_evidence_paths(inputs, raw_logs, command_logs)
    artifacts = validate_artifact_assignments(args.artifact)
    gateway_profile = validate_gateway_profile(args.gateway_profile)
    # The profile is a separately deployed input, not a fourth spelling of an
    # artifact, repository, or evidence file.  Reject every lexical alias
    # before creating --out so the sealed bundle has one unambiguous source for
    # each role (including cross-category aliases that per-category checks miss).
    all_collection_sources: list[tuple[str, Path]] = []
    all_collection_sources.extend((f"--repo {name}", path) for name, path in repository_paths.items())
    all_collection_sources.extend((f"--input {name}", path) for name, path, _ in inputs)
    all_collection_sources.extend((f"--raw-log {name}", path) for name, path, _ in raw_logs)
    all_collection_sources.extend((f"--command-log {name}", path) for name, path, _ in command_logs)
    all_collection_sources.extend((f"--artifact {name}", path) for name, path in artifacts)
    all_collection_sources.append(("--gateway-profile", gateway_profile))
    reject_duplicate_canonical_paths(all_collection_sources, "collection source paths")
    return repository_paths, inputs, raw_logs, command_logs, artifacts, gateway_profile, out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-id", required=True)
    parser.add_argument(
        "--out",
        required=True,
        help="new evidence directory outside every repo and existing manifest bundle",
    )
    parser.add_argument(
        "--repo",
        action="append",
        default=[],
        metavar="NAME=PATH",
        type=lambda value: parse_assignment(value, "--repo"),
        help="exactly ros2=PATH, mdds=PATH, and rmw_mdds=PATH",
    )
    parser.add_argument(
        "--input",
        action="append",
        default=[],
        metavar="NAME=PATH",
        type=lambda value: parse_assignment(value, "--input"),
        help=(
            "named script/config/evidence input; must include "
            + ",".join(sorted(REQUIRED_INPUT_NAMES))
            + "; additional inputs are allowed, including domain0_smoke when executed"
        ),
    )
    parser.add_argument(
        "--raw-log",
        action="append",
        default=[],
        metavar="NAME=PATH",
        type=lambda value: parse_assignment(value, "--raw-log"),
        help="raw log; exactly build, deploy, board_a, board_b, ds, gw, and rmw",
    )
    parser.add_argument(
        "--artifact",
        action="append",
        default=[],
        metavar="NAME=PATH",
        type=lambda value: parse_assignment(value, "--artifact"),
        help=(
            "exactly libmdds=.../libmdds.so, librmw_mdds=.../librmw_mdds.so, "
            "and mdds_gateway=.../mdds_gateway"
        ),
    )
    parser.add_argument(
        "--command-log",
        action="append",
        default=[],
        metavar="NAME=PATH",
        type=lambda value: parse_assignment(value, "--command-log"),
        help="named command transcript; must include build, deploy, board_a, board_b, ds, gw, and rmw",
    )
    parser.add_argument(
        "--gateway-profile",
        required=True,
        help="deployed mdds_gateway profile to hash-seal; collection does not execute it",
    )
    parser.add_argument("--note", action="append", default=[], type=lambda value: parse_assignment(value, "--note"))
    args = parser.parse_args()

    try:
        (
            repository_paths,
            inputs,
            raw_logs,
            command_logs,
            artifacts,
            gateway_profile_path,
            out,
        ) = validate_collection_plan(args)
    except (FileNotFoundError, RuntimeError, ValueError) as error:
        parser.error(str(error))
    out.mkdir(parents=True, exist_ok=False)
    assert_tree_has_no_symlinks(out, "evidence output")

    try:
        # Capture every worktree before any evidence copy.  Each worktree is
        # later re-read in this same order before manifest.v1.json is written.
        # This detects changes to the sealed fields in each repository's own
        # capture-to-recheck interval; it is deliberately not a global atomic
        # snapshot because the collector does not hold a cross-repository lock.
        repository_order = sorted(REQUIRED_REPOSITORIES)
        initial_states = {
            name: capture_git_state(repository_paths[name])
            for name in repository_order
        }
        repos = [
            write_git_snapshot(name, repository_paths[name], out, initial_states[name])
            for name in repository_order
        ]
        copied: dict[str, list[dict[str, Any]]] = {"inputs": [], "raw_logs": [], "command_logs": [], "artifacts": []}
        for name, raw, _ in sorted(inputs):
            copied_input = copy_input(
                raw,
                out / "inputs" / safe_name(name),
                label=f"--input {name}",
            )
            copied_input["name"] = name
            copied["inputs"].append(copied_input)
        for name, raw, _ in sorted(raw_logs):
            copied_raw_log = copy_input(
                raw,
                out / "raw_logs" / safe_name(name),
                label=f"--raw-log {name}",
                require_regular_file=True,
            )
            copied_raw_log["name"] = name
            copied["raw_logs"].append(copied_raw_log)
        for name, raw, _ in sorted(command_logs):
            copied_command_log = copy_input(
                raw,
                out / "commands" / safe_name(name),
                label=f"--command-log {name}",
                require_regular_file=True,
            )
            copied_command_log["name"] = name
            copied["command_logs"].append(copied_command_log)
        for name, raw in sorted(artifacts):
            artifact_root = out / "artifacts" / safe_name(name)
            artifact_root.mkdir(parents=True, exist_ok=False)
            copied_artifact = copy_input(
                raw,
                artifact_root,
                label=f"--artifact {name}",
                require_regular_file=True,
            )
            copied_artifact["name"] = name
            copied["artifacts"].append(copied_artifact)
        gateway_profile = copy_input(
            gateway_profile_path,
            out / "profiles" / "gateway",
            label="--gateway-profile",
            require_regular_file=True,
        )

        for name in repository_order:
            assert_git_state_stable(
                name,
                initial_states[name],
                capture_git_state(repository_paths[name]),
            )

        notes = {name: value for name, value in args.note}
        manifest: dict[str, Any] = {
            "schema": "mdds-verification-manifest/v1",
            "seal": {
                "kind": "hash-sealed-local",
                "immutable": False,
                "warning": "SHA-256 detects alteration only if a digest is retained independently; no signing or WORM retention was configured.",
            },
            "run_id": args.run_id,
            "collected_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
            "collector": {"python": sys.version, "platform": platform.platform()},
            "repository_stability": {
                "result": "PER_REPOSITORY_SEQUENTIAL_RECHECK_MATCHED",
                "compared": [*GIT_STABILITY_COMMAND_KEYS, "untracked_inventory"],
                "capture_order": repository_order,
                "recheck_order": repository_order,
                "scope": "per-repository",
                "cross_repository_snapshot_atomic": False,
                "cross_repository_write_lock_held": False,
                "warning": (
                    "Each repository was captured and rechecked sequentially. "
                    "No cross-repository write lock was held, so this is not an "
                    "atomic global cross-repository snapshot; a repository can "
                    "change after its own recheck or between rechecks."
                ),
            },
            "repositories": repos,
            "copied": copied,
            "gateway_profile": {
                **gateway_profile,
                "runtime_status": "NOT_EXECUTED_BY_MANIFEST",
                "runtime_boundary": (
                    "This collector sealed the supplied profile bytes but did not start "
                    "mdds_gateway.  A raw gateway log must independently identify the "
                    "profile/hash before it can prove that this profile was actually used."
                ),
            },
            "notes": notes,
        }
        json_dump(out / "manifest.v1.json", manifest)
        sums, manifest_digest = write_integrity_index(out)
        archive, archive_digest = write_deterministic_tar(out)
        print(json.dumps({
            "out": str(out),
            "manifest_sha256": manifest_digest,
            "integrity_index": str(sums),
            "archive": str(archive),
            "archive_sha256": archive_digest,
        }, ensure_ascii=False, sort_keys=True))
        return 0
    except Exception as error:
        print(f"manifest collection failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
