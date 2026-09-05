#!/usr/bin/env python3
"""Create and verify a fail-closed receipt for a clean OHOS dependency build."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import re
import shutil
import stat
import subprocess
import sys
from pathlib import Path, PurePosixPath
from typing import Any, Iterable


RECEIPT_SCHEMA = "ros2-ohos-clean-target-deps-receipt-v1"
MANIFEST_SCHEMA = "ros2-ohos-clean-target-deps-prefix-v1"
JOURNAL_SCHEMA = "ros2-ohos-target-deps-journal-v1"
CHUNK_SIZE = 1024 * 1024
HEX_SHA256 = re.compile(r"[0-9a-f]{64}")
HEX_GIT = re.compile(r"(?:[0-9a-f]{40}|[0-9a-f]{64})")
SAFE_NAME = re.compile(r"[A-Za-z0-9_.+-]+")
UTC_TIMESTAMP = re.compile(r"[0-9]{4}-[0-9]{2}-[0-9]{2}T[^\s]+Z")

MANIFEST_NAME = ".ohos-target-deps.clean-prefix.json"
RECEIPT_NAME = ".ohos-target-deps.clean-receipt.json"
PREFIX_ARTIFACTS = frozenset({MANIFEST_NAME, RECEIPT_NAME})
EXPECTED_RECIPES = frozenset(
    {
        "qt_host_tools",
        "core_target_deps",
        "qtbase",
        "assimp",
        "ogre",
        "qtsvg",
        "pyqt_sip",
        "pyqt5",
    }
)
RESOLVED_HOST_COMMANDS = (
    "sh",
    "bash",
    "tar",
    "patch",
    "which",
    "sha256sum",
    "make",
    "cmake",
    "ninja",
    "curl",
    "git",
)


class ReceiptError(RuntimeError):
    """Raised when a clean-build provenance invariant is not satisfied."""


def utc_now() -> str:
    return (
        dt.datetime.now(dt.timezone.utc)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z")
    )


def canonical_json(value: Any) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        + "\n"
    ).encode("utf-8")


def pretty_json(value: Any) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def absolute(path: Path) -> Path:
    return Path(os.path.abspath(os.fspath(path)))


def lexists(path: Path) -> bool:
    return os.path.lexists(os.fspath(path))


def require_directory(path: Path, label: str) -> Path:
    path = absolute(path)
    try:
        metadata = path.lstat()
    except FileNotFoundError as exc:
        raise ReceiptError(f"{label} is missing: {path}") from exc
    if stat.S_ISLNK(metadata.st_mode) or not stat.S_ISDIR(metadata.st_mode):
        raise ReceiptError(f"{label} must be a non-symlink directory: {path}")
    return path


def require_regular(path: Path, label: str) -> Path:
    path = absolute(path)
    try:
        metadata = path.lstat()
    except FileNotFoundError as exc:
        raise ReceiptError(f"{label} is missing: {path}") from exc
    if stat.S_ISLNK(metadata.st_mode) or not stat.S_ISREG(metadata.st_mode):
        raise ReceiptError(f"{label} must be a non-symlink regular file: {path}")
    return path


def sha256_file(path: Path) -> str:
    path = require_regular(path, "hashed file")
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(CHUNK_SIZE), b""):
            digest.update(block)
    return digest.hexdigest()


def path_label(path: Path, workspace: Path) -> str:
    path = absolute(path)
    workspace = absolute(workspace)
    try:
        return path.relative_to(workspace).as_posix()
    except ValueError:
        return str(path)


def recorded_path(label: str, workspace: Path, description: str) -> Path:
    if not isinstance(label, str) or not label or "\0" in label:
        raise ReceiptError(f"invalid recorded {description} path")
    raw = Path(label)
    result = absolute(raw if raw.is_absolute() else workspace / raw)
    if not raw.is_absolute():
        try:
            result.resolve(strict=False).relative_to(workspace.resolve(strict=True))
        except ValueError as exc:
            raise ReceiptError(f"recorded {description} path escapes workspace: {label}") from exc
    return result


def mode_text(mode: int) -> str:
    return f"{stat.S_IMODE(mode):04o}"


def _validate_relative_symlink(relative: str, target: str) -> None:
    """Validate a link lexically without following it (dangling links are allowed)."""

    if not target or "\0" in target:
        raise ReceiptError(f"empty or invalid symlink target at {relative}")
    portable = target.replace("\\", "/")
    if portable.startswith("/") or re.match(r"^[A-Za-z]:", portable):
        raise ReceiptError(f"absolute symlink is forbidden at {relative}: {target}")
    stack = list(PurePosixPath(relative).parent.parts)
    for part in PurePosixPath(portable).parts:
        if part in ("", "."):
            continue
        if part == "..":
            if not stack:
                raise ReceiptError(f"symlink escapes its tree at {relative}: {target}")
            stack.pop()
        else:
            stack.append(part)


def _entry(path: Path, relative: str, metadata: os.stat_result) -> dict[str, Any]:
    common: dict[str, Any] = {
        "path": relative,
        "mode": mode_text(metadata.st_mode),
        "size": 0,
        "sha256": None,
        "link": None,
    }
    if stat.S_ISDIR(metadata.st_mode):
        common["type"] = "directory"
        return common
    if stat.S_ISREG(metadata.st_mode):
        common.update(
            {
                "type": "file",
                "size": metadata.st_size,
                "sha256": sha256_file(path),
            }
        )
        return common
    if stat.S_ISLNK(metadata.st_mode):
        target = os.readlink(path)
        _validate_relative_symlink(relative, target)
        common.update(
            {
                "type": "symlink",
                "size": len(os.fsencode(target)),
                "link": target,
            }
        )
        return common
    raise ReceiptError(f"special filesystem entry is forbidden: {path}")


def tree_inventory(root: Path, excluded: Iterable[str] = ()) -> list[dict[str, Any]]:
    root = require_directory(root, "inventory root")
    excluded_set = frozenset(excluded)
    entries = [_entry(root, ".", root.lstat())]

    def visit(directory: Path, parent: PurePosixPath) -> None:
        try:
            children = sorted(os.scandir(directory), key=lambda item: item.name)
        except OSError as exc:
            raise ReceiptError(f"cannot enumerate inventory directory: {directory}: {exc}") from exc
        for child in children:
            relative_path = parent / child.name
            relative = relative_path.as_posix()
            if relative in excluded_set:
                continue
            path = Path(child.path)
            metadata = child.stat(follow_symlinks=False)
            entry = _entry(path, relative, metadata)
            entries.append(entry)
            if entry["type"] == "directory":
                visit(path, relative_path)

    visit(root, PurePosixPath())
    return entries


def inventory_summary(root: Path, workspace: Path) -> dict[str, Any]:
    entries = tree_inventory(root)
    return {
        "path": path_label(root, workspace),
        "entries": len(entries),
        "file_bytes": sum(
            entry["size"] for entry in entries if entry["type"] == "file"
        ),
        "sha256": sha256_bytes(canonical_json(entries)),
    }


def file_record(path: Path, workspace: Path) -> dict[str, Any]:
    path = require_regular(path, "recorded input")
    metadata = path.lstat()
    return {
        "path": path_label(path, workspace),
        "type": "file",
        "mode": mode_text(metadata.st_mode),
        "size": metadata.st_size,
        "sha256": sha256_file(path),
        "link": None,
    }


def create_only(path: Path, payload: bytes, label: str) -> None:
    path = absolute(path)
    if lexists(path):
        raise ReceiptError(f"{label} is create-only and already exists: {path}")
    require_directory(path.parent, f"{label} parent")
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    if hasattr(os, "O_BINARY"):
        flags |= os.O_BINARY
    descriptor = os.open(path, flags, 0o600)
    try:
        view = memoryview(payload)
        while view:
            written = os.write(descriptor, view)
            if written <= 0:
                raise ReceiptError(f"short write while creating {label}: {path}")
            view = view[written:]
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def run(argv: list[str], *, cwd: Path | None = None) -> bytes:
    completed = subprocess.run(
        argv,
        cwd=str(cwd) if cwd else None,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if completed.returncode != 0:
        raise ReceiptError(
            f"command failed ({completed.returncode}): {' '.join(argv)}\n"
            f"{completed.stderr.decode('utf-8', 'replace')}"
        )
    return completed.stdout


def validate_workspace(workspace: Path) -> Path:
    workspace = require_directory(workspace, "workspace")
    root = absolute(
        Path(run(["git", "-C", str(workspace), "rev-parse", "--show-toplevel"]).decode().strip())
    )
    if root.resolve(strict=True) != workspace.resolve(strict=True):
        raise ReceiptError(f"workspace is not the Git worktree root: {workspace} (root={root})")
    require_directory(workspace / "target_deps_src", "target_deps_src")
    return workspace


def ignored_target_deps(workspace: Path) -> list[str]:
    raw = run(
        [
            "git",
            "-C",
            str(workspace),
            "status",
            "--ignored",
            "--porcelain=v1",
            "-z",
            "--",
            "target_deps_src",
        ]
    )
    ignored: list[str] = []
    for record in raw.split(b"\0"):
        if record.startswith(b"!! "):
            ignored.append(record[3:].decode("utf-8", "surrogateescape"))
    return ignored


def recipe_file_inventory(workspace: Path) -> list[dict[str, Any]]:
    raw = run(
        [
            "git",
            "-C",
            str(workspace),
            "ls-files",
            "--cached",
            "--others",
            "--exclude-standard",
            "-z",
            "--",
            "target_deps_src",
            "cmake",
            "scripts/build_target_deps.sh",
            "scripts/python_target.py",
        ]
    )
    relative_paths = {
        item.decode("utf-8", "surrogateescape")
        for item in raw.split(b"\0")
        if item
    }
    relative_paths.update(
        {
            ".gitignore",
            "pixi.toml",
            "pixi.lock",
            "cmake/ohos-aarch64.toolchain.cmake",
            "scripts/python/ohos_python.lock.json",
            "scripts/python_target.py",
            "scripts/build_target_deps.sh",
        }
    )
    entries: list[dict[str, Any]] = []
    for relative in sorted(relative_paths):
        path = absolute(workspace / relative)
        if not lexists(path):
            # A tracked deletion is not an input to the candidate recipe.
            continue
        try:
            path.relative_to(workspace)
        except ValueError as exc:
            raise ReceiptError(f"recipe input escapes workspace: {relative}") from exc
        metadata = path.lstat()
        if stat.S_ISDIR(metadata.st_mode):
            raise ReceiptError(f"Git recipe input unexpectedly names a directory: {relative}")
        entry = _entry(path, Path(relative).as_posix(), metadata)
        if entry["type"] != "file":
            raise ReceiptError(f"clean dependency recipe input must be a regular file: {relative}")
        entries.append(entry)
    mandatory = {
        ".gitignore",
        "pixi.toml",
        "pixi.lock",
        "cmake/ohos-aarch64.toolchain.cmake",
        "scripts/python/ohos_python.lock.json",
        "scripts/python_target.py",
        "scripts/build_target_deps.sh",
        "target_deps_src/sources.lock",
        "target_deps_src/lib/clean_deps_receipt.py",
        "target_deps_src/lib/locked_sources.sh",
        "target_deps_src/build_all_clean_ohos.sh",
    }
    present = {entry["path"] for entry in entries}
    missing = sorted(mandatory - present)
    if missing:
        raise ReceiptError(f"clean dependency recipe inputs are missing: {missing}")
    return entries


def resolve_input(workspace: Path, value: Path, default: str) -> Path:
    path = value if value is not None else Path(default)
    return absolute(path if path.is_absolute() else workspace / path)


def host_tool_records(workspace: Path) -> list[dict[str, Any]]:
    # Record what PATH really selects.  In particular, the clean runner keeps
    # Git Bash's /usr/bin ahead of pixi because pixi's sh executables start a
    # different MSYS runtime and can discard Library/bin/make.exe.  Fixed
    # package locations alone would therefore describe tools that never ran.
    candidates: list[tuple[str, Path]] = [
        ("python", workspace / ".pixi/envs/default/python.exe"),
        # The resolved pixi git.exe is a launcher; bind its payload as well.
        ("git_payload", workspace / ".pixi/envs/default/Library/mingw64/bin/git.exe"),
    ]
    for name in RESOLVED_HOST_COMMANDS:
        resolved = shutil.which(name)
        if not resolved:
            raise ReceiptError(f"required host command is not resolvable: {name}")
        candidates.append((name, Path(resolved)))
    records: list[dict[str, Any]] = []
    for name, path in candidates:
        record = file_record(require_regular(path, f"host {name}"), workspace)
        record["name"] = name
        records.append(record)
    return records


def verify_recorded_host_tools(records: Any, workspace: Path) -> None:
    if not isinstance(records, list):
        raise ReceiptError("BEGIN receipt lacks its actual host tool records")
    required_names = {"python", "git_payload", *RESOLVED_HOST_COMMANDS}
    names = [record.get("name") for record in records if isinstance(record, dict)]
    if len(names) != len(records) or len(names) != len(set(names)) or set(names) != required_names:
        raise ReceiptError("BEGIN receipt has an invalid actual host tool set")
    for expected in records:
        name = expected["name"]
        current = current_recorded_file(expected, workspace, f"host tool {name}")
        current["name"] = name
        if current != expected:
            raise ReceiptError(f"actual host build tool changed after BEGIN: {name}")


def host_frontend_records(workspace: Path) -> list[dict[str, Any]]:
    """Identify the exact SIP/PyQt frontend that executed after bootstrap."""

    records: list[dict[str, Any]] = []
    for name, path in (
        ("sipbuild", workspace / ".pixi/envs/default/Lib/site-packages/sipbuild"),
        (
            "sip_dist_info",
            workspace / ".pixi/envs/default/Lib/site-packages/sip-6.8.6.dist-info",
        ),
        ("pyqtbuild", workspace / ".pixi/envs/default/Lib/site-packages/pyqtbuild"),
        (
            "pyqt_builder_dist_info",
            workspace
            / ".pixi/envs/default/Lib/site-packages/pyqt_builder-1.19.1.dist-info",
        ),
    ):
        record = inventory_summary(require_directory(path, f"host frontend {name}"), workspace)
        record["name"] = name
        records.append(record)
    executable = file_record(
        workspace / ".pixi/envs/default/Scripts/sip-build.exe", workspace
    )
    executable["name"] = "sip-build"
    records.append(executable)
    return records


def load_sources_lock(path: Path) -> dict[str, dict[str, str]]:
    path = require_regular(path, "target dependency source lock")
    records: dict[str, dict[str, str]] = {}
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        fields = stripped.split()
        if len(fields) != 4:
            raise ReceiptError(f"invalid sources.lock line {line_number}: expected four fields")
        kind, name, digest, url = fields
        if kind not in {"archive", "git"}:
            raise ReceiptError(f"invalid source kind on line {line_number}: {kind}")
        if not SAFE_NAME.fullmatch(name) or name in records:
            raise ReceiptError(f"invalid or duplicate source name on line {line_number}: {name}")
        digest_pattern = HEX_SHA256 if kind == "archive" else HEX_GIT
        if not digest_pattern.fullmatch(digest):
            raise ReceiptError(f"invalid {kind} digest on line {line_number}: {digest}")
        if not url.startswith("https://") or any(character.isspace() for character in url):
            raise ReceiptError(f"source URL must be HTTPS on line {line_number}: {url}")
        records[name] = {"kind": kind, "digest": digest, "url": url}
    if not records:
        raise ReceiptError("target dependency source lock is empty")
    return records


def checked_json(path: Path, label: str) -> dict[str, Any]:
    path = require_regular(path, label)
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise ReceiptError(f"invalid {label}: {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ReceiptError(f"{label} must contain one JSON object: {path}")
    return value


def flatten_expected(values: list[list[str]]) -> list[str]:
    names = [name for group in values for name in group]
    if any(not SAFE_NAME.fullmatch(name) for name in names):
        raise ReceiptError("expected recipe list contains an unsafe name")
    if len(names) != len(set(names)):
        raise ReceiptError("expected recipe list contains duplicates")
    if set(names) != EXPECTED_RECIPES:
        raise ReceiptError(
            "expected recipe set differs from the complete clean profile: "
            f"expected={sorted(EXPECTED_RECIPES)} actual={sorted(names)}"
        )
    return sorted(names)


def begin(args: argparse.Namespace) -> None:
    workspace = validate_workspace(args.workspace)
    prefix = absolute(args.prefix)
    output = absolute(args.output)
    journal = absolute(args.journal)
    for path, label in ((prefix, "dependency prefix"), (output, "BEGIN receipt"), (journal, "journal")):
        if lexists(path):
            raise ReceiptError(f"clean begin requires {label} path to be absent: {path}")
    ignored = ignored_target_deps(workspace)
    if ignored:
        raise ReceiptError(
            "clean begin refuses Git-ignored target_deps_src cache entries: "
            + repr(sorted(ignored))
        )
    expected_recipes = flatten_expected(args.expected_recipe)
    sources_lock = resolve_input(workspace, args.sources_lock, "target_deps_src/sources.lock")
    pixi_lock = resolve_input(workspace, args.pixi_lock, "pixi.lock")
    python_lock = resolve_input(
        workspace, args.python_lock, "scripts/python/ohos_python.lock.json"
    )
    toolchain = resolve_input(
        workspace, args.toolchain, "cmake/ohos-aarch64.toolchain.cmake"
    )
    load_sources_lock(sources_lock)
    inputs = {
        "files": {
            "sources_lock": file_record(sources_lock, workspace),
            "pixi_lock": file_record(pixi_lock, workspace),
            "python_lock": file_record(python_lock, workspace),
            "toolchain": file_record(toolchain, workspace),
        },
        "recipe_files": recipe_file_inventory(workspace),
        "host_tools": host_tool_records(workspace),
        "sdk_tree": inventory_summary(args.sdk_root, workspace),
        "python_target_usr_tree": inventory_summary(args.python_target_root, workspace),
        "python_sitepkgs_tree": inventory_summary(args.python_sitepkgs_root, workspace),
    }
    record: dict[str, Any] = {
        "schema": RECEIPT_SCHEMA,
        "phase": "BEGIN",
        "created_utc": utc_now(),
        "workspace": str(workspace),
        "prefix": path_label(prefix, workspace),
        "journal": path_label(journal, workspace),
        "expected_recipes": expected_recipes,
        "inputs": inputs,
    }
    record["begin_payload_sha256"] = sha256_bytes(canonical_json(record))
    create_only(output, pretty_json(record), "BEGIN receipt")
    print(
        "TARGET_DEPS_RECEIPT_BEGIN"
        f" inputs={len(inputs['recipe_files'])}"
        f" sdk_sha256={inputs['sdk_tree']['sha256']}"
        f" recipes={len(expected_recipes)}"
    )


def validate_journal_arguments(args: argparse.Namespace) -> dict[str, Any]:
    if not SAFE_NAME.fullmatch(args.name or ""):
        raise ReceiptError("journal --name must be one safe non-empty identifier")
    record: dict[str, Any] = {
        "schema": JOURNAL_SCHEMA,
        "event": args.event,
        "name": args.name,
        "created_utc": utc_now(),
    }
    if args.event == "source_verified":
        if args.kind not in {"archive", "git"}:
            raise ReceiptError("source_verified requires --kind archive|git")
        pattern = HEX_SHA256 if args.kind == "archive" else HEX_GIT
        if not pattern.fullmatch(args.digest or ""):
            raise ReceiptError("source_verified has an invalid --digest")
        if not args.url or not args.url.startswith("https://") or any(
            character.isspace() for character in args.url
        ):
            raise ReceiptError("source_verified requires one HTTPS --url")
        if not args.path or "\0" in args.path:
            raise ReceiptError("source_verified requires a safe non-empty --path")
        if args.target is not None:
            raise ReceiptError("source_verified does not accept --target")
        record.update(
            {"kind": args.kind, "digest": args.digest, "url": args.url, "path": args.path}
        )
    elif args.event == "patch_applied":
        if args.kind is not None or args.url is not None:
            raise ReceiptError("patch_applied does not accept --kind or --url")
        if not HEX_SHA256.fullmatch(args.digest or ""):
            raise ReceiptError("patch_applied requires a SHA-256 --digest")
        if not args.path or not args.target or "\0" in args.path or "\0" in args.target:
            raise ReceiptError("patch_applied requires safe non-empty --path and --target")
        record.update({"digest": args.digest, "path": args.path, "target": args.target})
    elif args.event == "recipe_complete":
        if args.name not in EXPECTED_RECIPES:
            raise ReceiptError(f"unknown clean dependency recipe: {args.name}")
        if any(value is not None for value in (args.kind, args.digest, args.url, args.path, args.target)):
            raise ReceiptError("recipe_complete accepts only --journal, --event and --name")
    else:
        raise ReceiptError(f"unknown journal event: {args.event}")
    return record


def append_journal(path: Path, record: dict[str, Any]) -> None:
    path = absolute(path)
    require_directory(path.parent, "journal parent")
    if lexists(path):
        metadata = path.lstat()
        if stat.S_ISLNK(metadata.st_mode) or not stat.S_ISREG(metadata.st_mode):
            raise ReceiptError(f"journal must be a non-symlink regular file: {path}")
    flags = os.O_RDWR | os.O_APPEND | os.O_CREAT
    if hasattr(os, "O_BINARY"):
        flags |= os.O_BINARY
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    descriptor = os.open(path, flags, 0o600)
    try:
        metadata = os.fstat(descriptor)
        if not stat.S_ISREG(metadata.st_mode):
            raise ReceiptError(f"journal descriptor is not a regular file: {path}")
        if metadata.st_size:
            os.lseek(descriptor, -1, os.SEEK_END)
            if os.read(descriptor, 1) != b"\n":
                raise ReceiptError(f"journal has a truncated final record: {path}")
        payload = canonical_json(record)
        view = memoryview(payload)
        while view:
            written = os.write(descriptor, view)
            if written <= 0:
                raise ReceiptError(f"short append to journal: {path}")
            view = view[written:]
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def journal(args: argparse.Namespace) -> None:
    record = validate_journal_arguments(args)
    append_journal(args.journal, record)
    print(f"TARGET_DEPS_JOURNAL event={record['event']} name={record['name']}")


def validate_begin_record(record: dict[str, Any]) -> None:
    required = {
        "schema",
        "phase",
        "created_utc",
        "workspace",
        "prefix",
        "journal",
        "expected_recipes",
        "inputs",
        "begin_payload_sha256",
    }
    if set(record) != required:
        raise ReceiptError("BEGIN receipt has unexpected or missing top-level fields")
    if record.get("schema") != RECEIPT_SCHEMA or record.get("phase") != "BEGIN":
        raise ReceiptError("BEGIN receipt has the wrong schema or phase")
    claim = record.get("begin_payload_sha256")
    payload = dict(record)
    payload.pop("begin_payload_sha256", None)
    if not isinstance(claim, str) or not HEX_SHA256.fullmatch(claim):
        raise ReceiptError("BEGIN receipt lacks a valid payload digest")
    if sha256_bytes(canonical_json(payload)) != claim:
        raise ReceiptError("BEGIN receipt payload digest mismatch")
    if record.get("expected_recipes") != sorted(EXPECTED_RECIPES):
        raise ReceiptError("BEGIN receipt does not require the complete clean recipe set")


def current_recorded_file(record: dict[str, Any], workspace: Path, label: str) -> dict[str, Any]:
    if not isinstance(record, dict) or not isinstance(record.get("path"), str):
        raise ReceiptError(f"invalid recorded file identity for {label}")
    path = recorded_path(record["path"], workspace, label)
    return file_record(path, workspace)


def verify_begin_inputs(record: dict[str, Any], workspace: Path) -> None:
    validate_begin_record(record)
    if absolute(Path(record["workspace"])).resolve(strict=True) != workspace.resolve(strict=True):
        raise ReceiptError("BEGIN receipt belongs to another workspace")
    inputs = record.get("inputs")
    if not isinstance(inputs, dict) or set(inputs) != {
        "files",
        "recipe_files",
        "host_tools",
        "sdk_tree",
        "python_target_usr_tree",
        "python_sitepkgs_tree",
    }:
        raise ReceiptError("BEGIN receipt has an invalid input section")
    files = inputs.get("files")
    if not isinstance(files, dict) or set(files) != {
        "sources_lock",
        "pixi_lock",
        "python_lock",
        "toolchain",
    }:
        raise ReceiptError("BEGIN receipt lacks its fixed input file set")
    for name, expected in files.items():
        if current_recorded_file(expected, workspace, name) != expected:
            raise ReceiptError(f"clean dependency input changed after BEGIN: {name}")
    if recipe_file_inventory(workspace) != inputs.get("recipe_files"):
        raise ReceiptError("workspace dependency recipe file inventory changed after BEGIN")
    verify_recorded_host_tools(inputs.get("host_tools"), workspace)
    for key, description in (
        ("sdk_tree", "OHOS SDK"),
        ("python_target_usr_tree", "python_target/usr"),
        ("python_sitepkgs_tree", "python_target/sitepkgs"),
    ):
        expected = inputs.get(key)
        if not isinstance(expected, dict) or not isinstance(expected.get("path"), str):
            raise ReceiptError(f"BEGIN receipt has an invalid {description} tree record")
        root = recorded_path(expected["path"], workspace, description)
        if inventory_summary(root, workspace) != expected:
            raise ReceiptError(f"{description} tree changed after BEGIN")


def load_journal(path: Path) -> tuple[list[dict[str, Any]], bytes]:
    path = require_regular(path, "target dependency journal")
    payload = path.read_bytes()
    if not payload or not payload.endswith(b"\n"):
        raise ReceiptError("target dependency journal is empty or truncated")
    events: list[dict[str, Any]] = []
    for line_number, line in enumerate(payload.splitlines(), 1):
        if not line:
            raise ReceiptError(f"blank journal record at line {line_number}")
        try:
            event = json.loads(line.decode("utf-8"))
        except (UnicodeError, json.JSONDecodeError) as exc:
            raise ReceiptError(f"invalid journal JSON at line {line_number}: {exc}") from exc
        if not isinstance(event, dict):
            raise ReceiptError(f"journal line {line_number} is not an object")
        expected_fields = {
            "source_verified": {"schema", "event", "name", "created_utc", "kind", "digest", "url", "path"},
            "patch_applied": {"schema", "event", "name", "created_utc", "digest", "path", "target"},
            "recipe_complete": {"schema", "event", "name", "created_utc"},
        }.get(event.get("event"))
        if expected_fields is None or set(event) != expected_fields:
            raise ReceiptError(f"journal line {line_number} has unknown fields or event type")
        if event.get("schema") != JOURNAL_SCHEMA:
            raise ReceiptError(f"journal line {line_number} has the wrong schema")
        if not isinstance(event.get("created_utc"), str) or not UTC_TIMESTAMP.fullmatch(
            event["created_utc"]
        ):
            raise ReceiptError(f"journal line {line_number} has an invalid timestamp")
        events.append(event)
    return events, payload


def event_path(raw: Any, workspace: Path, label: str) -> Path:
    if not isinstance(raw, str) or not raw or "\0" in raw:
        raise ReceiptError(f"invalid {label} path in journal")
    candidate = Path(raw)
    path = absolute(candidate if candidate.is_absolute() else workspace / candidate)
    try:
        path.resolve(strict=False).relative_to((workspace / "target_deps_src").resolve(strict=True))
    except ValueError as exc:
        raise ReceiptError(f"journal {label} escapes target_deps_src: {raw}") from exc
    return path


def verify_archive_source(path: Path, digest: str, name: str) -> None:
    if sha256_file(require_regular(path, f"verified source {name}")) != digest:
        raise ReceiptError(f"verified archive bytes changed after journal event: {name}")


def verify_git_source(path: Path, digest: str, name: str) -> None:
    path = require_directory(path, f"verified Git source {name}")
    actual = run(["git", "-C", str(path), "rev-parse", "HEAD"]).decode("ascii", "strict").strip()
    if actual != digest:
        raise ReceiptError(f"verified Git source HEAD changed: {name}: {actual} != {digest}")


def validate_journal(
    events: list[dict[str, Any]], begin_record: dict[str, Any], workspace: Path
) -> dict[str, Any]:
    lock_record = begin_record["inputs"]["files"]["sources_lock"]
    lock_path = recorded_path(lock_record["path"], workspace, "sources.lock")
    locked = load_sources_lock(lock_path)
    sources: dict[str, list[dict[str, Any]]] = {}
    patches: list[dict[str, Any]] = []
    recipes: list[str] = []
    for event in events:
        if event["event"] == "source_verified":
            name = event.get("name")
            if name not in locked:
                raise ReceiptError(f"journal contains an unlocked source name: {name}")
            identity = {key: event.get(key) for key in ("kind", "digest", "url")}
            if identity != locked[name]:
                raise ReceiptError(f"journal source identity differs from sources.lock: {name}")
            path = event_path(event.get("path"), workspace, f"source {name}")
            if event["kind"] == "archive":
                verify_archive_source(path, event["digest"], name)
            else:
                verify_git_source(path, event["digest"], name)
            sources.setdefault(name, []).append(event)
        elif event["event"] == "patch_applied":
            patches.append(event)
        else:
            recipes.append(event.get("name"))
    missing_sources = sorted(set(locked) - set(sources))
    if missing_sources:
        raise ReceiptError(f"journal lacks verified source events: {missing_sources}")

    recipe_inputs = begin_record["inputs"]["recipe_files"]
    expected_patch_records = {
        entry["path"]: entry
        for entry in recipe_inputs
        if isinstance(entry.get("path"), str)
        and entry["path"].startswith("target_deps_src/")
        and entry["path"].endswith(".patch")
    }
    seen_patches: set[str] = set()
    for event in patches:
        patch_path = event_path(event.get("path"), workspace, f"patch {event.get('name')}")
        patch_label = path_label(patch_path, workspace)
        expected = expected_patch_records.get(patch_label)
        if expected is None:
            raise ReceiptError(f"journal contains an unknown patch path: {patch_label}")
        if event.get("name") != patch_path.name:
            raise ReceiptError(f"journal patch name/path mismatch: {event.get('name')}")
        if event.get("digest") != expected.get("sha256") or sha256_file(patch_path) != event.get(
            "digest"
        ):
            raise ReceiptError(f"journal patch digest mismatch: {patch_label}")
        patch_target = event_path(
            event.get("target"), workspace, f"patch target {event.get('name')}"
        )
        require_directory(patch_target, f"patch target {event.get('name')}")
        if patch_label in seen_patches:
            raise ReceiptError(f"duplicate patch event in clean journal: {patch_label}")
        seen_patches.add(patch_label)
    missing_patches = sorted(set(expected_patch_records) - seen_patches)
    if missing_patches:
        raise ReceiptError(f"journal lacks patch_applied events: {missing_patches}")

    if any(not isinstance(name, str) or not SAFE_NAME.fullmatch(name) for name in recipes):
        raise ReceiptError("journal contains an invalid recipe_complete name")
    if len(recipes) != len(set(recipes)):
        raise ReceiptError("journal contains duplicate recipe_complete events")
    expected_recipes = set(begin_record["expected_recipes"])
    if set(recipes) != expected_recipes:
        raise ReceiptError(
            "journal recipe completion set differs: "
            f"missing={sorted(expected_recipes - set(recipes))} "
            f"extra={sorted(set(recipes) - expected_recipes)}"
        )
    return {
        "event_count": len(events),
        "source_names": sorted(sources),
        "source_event_counts": {name: len(sources[name]) for name in sorted(sources)},
        "patches": sorted(seen_patches),
        "recipes": sorted(recipes),
    }


def fixed_artifact_paths(prefix: Path, manifest: Path, receipt: Path) -> tuple[Path, Path]:
    prefix = require_directory(prefix, "dependency prefix")
    manifest = absolute(manifest)
    receipt = absolute(receipt)
    expected_manifest = absolute(prefix / MANIFEST_NAME)
    expected_receipt = absolute(prefix / RECEIPT_NAME)
    if manifest != expected_manifest or receipt != expected_receipt:
        raise ReceiptError(
            f"clean dependency artifacts must be {expected_manifest} and {expected_receipt}"
        )
    return manifest, receipt


def make_manifest(prefix: Path, workspace: Path) -> dict[str, Any]:
    entries = tree_inventory(prefix, PREFIX_ARTIFACTS)
    if not any(entry["type"] == "file" for entry in entries):
        raise ReceiptError("clean dependency prefix contains no regular-file output")
    record: dict[str, Any] = {
        "schema": MANIFEST_SCHEMA,
        "created_utc": utc_now(),
        "prefix": path_label(prefix, workspace),
        "entry_count": len(entries),
        "file_bytes": sum(entry["size"] for entry in entries if entry["type"] == "file"),
        "inventory_sha256": sha256_bytes(canonical_json(entries)),
        "entries": entries,
    }
    record["manifest_payload_sha256"] = sha256_bytes(canonical_json(record))
    return record


def validate_manifest(record: dict[str, Any], prefix: Path, workspace: Path) -> None:
    required = {
        "schema",
        "created_utc",
        "prefix",
        "entry_count",
        "file_bytes",
        "inventory_sha256",
        "entries",
        "manifest_payload_sha256",
    }
    if set(record) != required or record.get("schema") != MANIFEST_SCHEMA:
        raise ReceiptError("prefix manifest has the wrong schema or fields")
    claim = record.get("manifest_payload_sha256")
    payload = dict(record)
    payload.pop("manifest_payload_sha256", None)
    if not isinstance(claim, str) or not HEX_SHA256.fullmatch(claim):
        raise ReceiptError("prefix manifest lacks a valid self digest")
    if sha256_bytes(canonical_json(payload)) != claim:
        raise ReceiptError("prefix manifest self digest mismatch")
    entries = record.get("entries")
    if not isinstance(entries, list):
        raise ReceiptError("prefix manifest entries are not a list")
    if record.get("entry_count") != len(entries):
        raise ReceiptError("prefix manifest entry count mismatch")
    if record.get("file_bytes") != sum(
        entry.get("size", -1)
        for entry in entries
        if isinstance(entry, dict) and entry.get("type") == "file"
    ):
        raise ReceiptError("prefix manifest byte count mismatch")
    if record.get("inventory_sha256") != sha256_bytes(canonical_json(entries)):
        raise ReceiptError("prefix manifest inventory digest mismatch")
    if recorded_path(record.get("prefix"), workspace, "prefix manifest root") != prefix:
        raise ReceiptError("prefix manifest belongs to another prefix")
    if tree_inventory(prefix, PREFIX_ARTIFACTS) != entries:
        raise ReceiptError("dependency prefix exact inventory differs from its manifest")


def finish(args: argparse.Namespace) -> None:
    workspace = validate_workspace(args.workspace)
    prefix = require_directory(args.prefix, "dependency prefix")
    manifest_path, receipt_path = fixed_artifact_paths(
        prefix, args.manifest, args.output
    )
    for path, label in ((manifest_path, "prefix manifest"), (receipt_path, "COMPLETE receipt")):
        if lexists(path):
            raise ReceiptError(f"{label} is create-only and already exists: {path}")
    begin_path = require_regular(args.begin, "BEGIN receipt")
    begin_record = checked_json(begin_path, "BEGIN receipt")
    verify_begin_inputs(begin_record, workspace)
    expected_prefix = recorded_path(begin_record["prefix"], workspace, "dependency prefix")
    if expected_prefix != prefix:
        raise ReceiptError("BEGIN receipt belongs to another dependency prefix")
    journal_path = require_regular(args.journal, "target dependency journal")
    expected_journal = recorded_path(begin_record["journal"], workspace, "journal")
    if expected_journal != journal_path:
        raise ReceiptError("BEGIN receipt belongs to another journal")
    events, journal_payload = load_journal(journal_path)
    journal_summary = validate_journal(events, begin_record, workspace)
    # Validate every remaining external input before creating either immutable
    # prefix artifact. A missing frontend must not leave an orphan manifest.
    host_frontends = host_frontend_records(workspace)
    manifest_record = make_manifest(prefix, workspace)
    create_only(manifest_path, pretty_json(manifest_record), "prefix manifest")
    if tree_inventory(prefix, PREFIX_ARTIFACTS) != manifest_record["entries"]:
        raise ReceiptError("dependency prefix changed while its manifest was written")
    receipt_record: dict[str, Any] = dict(begin_record)
    receipt_record.update(
        {
            "phase": "COMPLETE",
            "completed_utc": utc_now(),
            "result": "PASS",
            "begin_receipt": file_record(begin_path, workspace),
            "journal_result": {
                "path": path_label(journal_path, workspace),
                "size": len(journal_payload),
                "sha256": sha256_bytes(journal_payload),
                "events": events,
                **journal_summary,
            },
            "prefix_manifest": {
                **file_record(manifest_path, workspace),
                "manifest_payload_sha256": manifest_record["manifest_payload_sha256"],
                "inventory_sha256": manifest_record["inventory_sha256"],
                "entry_count": manifest_record["entry_count"],
                "file_bytes": manifest_record["file_bytes"],
            },
            # bootstrap_qt_host_tools force-reinstalls these from the locked
            # wheels before sip-build executes. Record the complete resulting
            # frontend, not merely its importlib version string.
            "host_frontends": host_frontends,
        }
    )
    receipt_record["complete_payload_sha256"] = sha256_bytes(
        canonical_json(receipt_record)
    )
    create_only(receipt_path, pretty_json(receipt_record), "COMPLETE receipt")
    print(
        "TARGET_DEPS_RECEIPT_COMPLETE"
        f" sources={len(journal_summary['source_names'])}"
        f" recipes={len(journal_summary['recipes'])}"
        f" prefix_sha256={manifest_record['inventory_sha256']}"
    )


def validate_complete_record(record: dict[str, Any]) -> dict[str, Any]:
    begin_fields = {
        "schema",
        "phase",
        "created_utc",
        "workspace",
        "prefix",
        "journal",
        "expected_recipes",
        "inputs",
        "begin_payload_sha256",
    }
    completion_fields = {
        "completed_utc",
        "result",
        "begin_receipt",
        "journal_result",
        "prefix_manifest",
        "host_frontends",
        "complete_payload_sha256",
    }
    if set(record) != begin_fields | completion_fields:
        raise ReceiptError("COMPLETE receipt has unexpected or missing top-level fields")
    if (
        record.get("schema") != RECEIPT_SCHEMA
        or record.get("phase") != "COMPLETE"
        or record.get("result") != "PASS"
    ):
        raise ReceiptError("receipt is not a completed clean dependency PASS")
    complete_claim = record.get("complete_payload_sha256")
    complete_payload = dict(record)
    complete_payload.pop("complete_payload_sha256", None)
    if not isinstance(complete_claim, str) or not HEX_SHA256.fullmatch(complete_claim):
        raise ReceiptError("COMPLETE receipt lacks a valid self digest")
    if sha256_bytes(canonical_json(complete_payload)) != complete_claim:
        raise ReceiptError("COMPLETE receipt self digest mismatch")
    begin_record = {key: record[key] for key in begin_fields}
    begin_record["phase"] = "BEGIN"
    validate_begin_record(begin_record)
    reconstructed = pretty_json(begin_record)
    identity = record.get("begin_receipt")
    if (
        not isinstance(identity, dict)
        or identity.get("size") != len(reconstructed)
        or identity.get("sha256") != sha256_bytes(reconstructed)
    ):
        raise ReceiptError("COMPLETE receipt does not bind its exact BEGIN JSON")
    return begin_record


def verify_embedded_journal(record: dict[str, Any], begin_record: dict[str, Any], workspace: Path) -> None:
    journal_result = record.get("journal_result")
    if not isinstance(journal_result, dict) or not isinstance(journal_result.get("events"), list):
        raise ReceiptError("COMPLETE receipt lacks its journal evidence")
    events = journal_result["events"]
    payload = b"".join(canonical_json(event) for event in events)
    if journal_result.get("size") != len(payload) or journal_result.get("sha256") != sha256_bytes(
        payload
    ):
        raise ReceiptError("embedded journal bytes do not match their recorded identity")
    summary = validate_journal(events, begin_record, workspace)
    for key, value in summary.items():
        if journal_result.get(key) != value:
            raise ReceiptError(f"embedded journal summary mismatch: {key}")


def verify(args: argparse.Namespace) -> None:
    workspace = validate_workspace(args.workspace)
    prefix = require_directory(args.prefix, "dependency prefix")
    manifest_path, receipt_path = fixed_artifact_paths(prefix, args.manifest, args.receipt)
    manifest_path = require_regular(manifest_path, "prefix manifest")
    receipt_path = require_regular(receipt_path, "COMPLETE receipt")
    receipt_record = checked_json(receipt_path, "COMPLETE receipt")
    begin_record = validate_complete_record(receipt_record)
    verify_begin_inputs(begin_record, workspace)
    if receipt_record.get("host_frontends") != host_frontend_records(workspace):
        raise ReceiptError("actual SIP/PyQt host frontend changed after COMPLETE")
    if recorded_path(begin_record["prefix"], workspace, "dependency prefix") != prefix:
        raise ReceiptError("COMPLETE receipt belongs to another dependency prefix")
    manifest_record = checked_json(manifest_path, "prefix manifest")
    validate_manifest(manifest_record, prefix, workspace)
    expected_manifest_identity = receipt_record.get("prefix_manifest")
    actual_manifest_identity = file_record(manifest_path, workspace)
    if not isinstance(expected_manifest_identity, dict):
        raise ReceiptError("COMPLETE receipt lacks a prefix manifest identity")
    for key, value in actual_manifest_identity.items():
        if expected_manifest_identity.get(key) != value:
            raise ReceiptError(f"prefix manifest file identity mismatch: {key}")
    for key in ("manifest_payload_sha256", "inventory_sha256", "entry_count", "file_bytes"):
        if expected_manifest_identity.get(key) != manifest_record.get(key):
            raise ReceiptError(f"prefix manifest receipt binding mismatch: {key}")
    verify_embedded_journal(receipt_record, begin_record, workspace)
    print(
        "TARGET_DEPS_RECEIPT_VERIFIED"
        f" sources={len(receipt_record['journal_result']['source_names'])}"
        f" recipes={len(receipt_record['journal_result']['recipes'])}"
        f" prefix_sha256={manifest_record['inventory_sha256']}"
    )


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    subparsers = result.add_subparsers(dest="command", required=True)

    start = subparsers.add_parser("begin")
    start.add_argument("--workspace", type=Path, required=True)
    start.add_argument("--prefix", "--install-root", dest="prefix", type=Path, required=True)
    start.add_argument("--sdk-root", type=Path, required=True)
    start.add_argument("--python-target-root", type=Path, required=True)
    start.add_argument("--python-sitepkgs-root", type=Path, required=True)
    start.add_argument("--journal", type=Path, required=True)
    start.add_argument("--expected-recipe", action="append", nargs="+", required=True)
    start.add_argument("--sources-lock", type=Path)
    start.add_argument("--pixi-lock", type=Path)
    start.add_argument("--python-lock", type=Path)
    start.add_argument("--toolchain", type=Path)
    start.add_argument("--output", type=Path, required=True)
    start.set_defaults(handler=begin)

    append = subparsers.add_parser("journal")
    append.add_argument("--journal", type=Path, required=True)
    append.add_argument(
        "--event",
        choices=("source_verified", "patch_applied", "recipe_complete"),
        required=True,
    )
    append.add_argument("--name", required=True)
    append.add_argument("--kind")
    append.add_argument("--digest")
    append.add_argument("--url")
    append.add_argument("--path")
    append.add_argument("--target")
    append.set_defaults(handler=journal)

    close = subparsers.add_parser("finish")
    close.add_argument("--workspace", type=Path, required=True)
    close.add_argument("--begin", type=Path, required=True)
    close.add_argument("--journal", type=Path, required=True)
    close.add_argument("--prefix", "--install-root", dest="prefix", type=Path, required=True)
    close.add_argument("--manifest", type=Path, required=True)
    close.add_argument("--output", "--receipt", dest="output", type=Path, required=True)
    close.set_defaults(handler=finish)

    check = subparsers.add_parser("verify")
    check.add_argument("--workspace", type=Path, required=True)
    check.add_argument("--prefix", "--install-root", dest="prefix", type=Path, required=True)
    check.add_argument("--manifest", type=Path, required=True)
    check.add_argument("--receipt", type=Path, required=True)
    check.set_defaults(handler=verify)
    return result


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        args.handler(args)
    except (ReceiptError, OSError, UnicodeError, ValueError, TypeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
