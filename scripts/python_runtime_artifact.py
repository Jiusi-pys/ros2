#!/usr/bin/env python3
"""Create-only, deterministic CPython runtime artifacts for RK3588A boards."""

from __future__ import annotations

import argparse
import ast
import datetime as dt
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import struct
import sys
import tarfile
import re
from typing import Any


WORKSPACE = Path(__file__).resolve().parent.parent
DEFAULT_LOCK = WORKSPACE / "scripts" / "python" / "ohos_python.lock.json"
MANAGER_PATH = WORKSPACE / "scripts" / "python_target.py"
REMOTE_RUNTIME_PREFIX = re.compile(
    r"^/data/[A-Za-z0-9._+-]+-verify-[0-9a-f]{12}$"
)
HEX_SHA256 = re.compile(r"^[0-9a-f]{64}$")
SOURCE_RECEIPT_NAME = "PYTHON_SOURCE_BUILD_RECEIPT.json"
SOURCE_RECEIPT_KIND = "cpython-ohos-source-build-receipt"
SOURCE_MODE = "source-reproducible"
ARTIFACT_MODE = "artifact-reproducible-not-source-reproducible"
RECIPE_ALGORITHM = "sha256-length-framed-path-size-content-v1"


class ArtifactError(RuntimeError):
    pass


def load_manager():
    spec = importlib.util.spec_from_file_location("python_target_manager", MANAGER_PATH)
    if spec is None or spec.loader is None:
        raise ArtifactError(f"cannot load {MANAGER_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ArtifactError(f"duplicate JSON key is forbidden: {key}")
        result[key] = value
    return result


def load_json_object(path: Path, label: str) -> dict[str, Any]:
    if path.is_symlink() or not path.is_file():
        raise ArtifactError(f"{label} must be a non-symlink regular file: {path}")
    try:
        value = json.loads(
            path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicate_keys
        )
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise ArtifactError(f"cannot read {label}: {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ArtifactError(f"{label} must contain a JSON object: {path}")
    return value


def require_sha256(value: Any, label: str) -> str:
    if not isinstance(value, str) or not HEX_SHA256.fullmatch(value):
        raise ArtifactError(f"{label} must be a lowercase SHA-256")
    return value


def require_positive_int(value: Any, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 1:
        raise ArtifactError(f"{label} must be a positive integer")
    return value


def canonical_json_sha256(value: Any) -> str:
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def remove_prefix(value: str, prefix: str) -> str:
    return value[len(prefix) :] if value.startswith(prefix) else value


def workspace_for_python_lock(lock_path: Path) -> Path:
    resolved = lock_path.resolve()
    if resolved.parent.name != "python" or resolved.parent.parent.name != "scripts":
        raise ArtifactError(
            "Python runtime lock must be located below WORKSPACE/scripts/python"
        )
    return resolved.parents[2]


def checked_workspace_file(workspace: Path, label: str) -> Path:
    pure = PurePosixPath(label)
    if pure.is_absolute() or ".." in pure.parts or not pure.parts:
        raise ArtifactError(f"unsafe repository-relative path: {label!r}")
    candidate = workspace.joinpath(*pure.parts)
    current = workspace
    for part in pure.parts:
        current = current / part
        if current.is_symlink():
            raise ArtifactError(f"recipe path traverses a symlink: {label}")
    if not candidate.is_file():
        raise ArtifactError(f"recipe file is missing: {label}")
    return candidate


def build_recipe_record(workspace: Path, source_lock: dict[str, Any]) -> dict[str, Any]:
    recipe = source_lock.get("build_recipe")
    if not isinstance(recipe, dict) or recipe.get("algorithm") != RECIPE_ALGORITHM:
        raise ArtifactError("unsupported or missing Python source build recipe")
    labels = recipe.get("files")
    if (
        not isinstance(labels, list)
        or not labels
        or any(not isinstance(item, str) for item in labels)
        or labels != sorted(set(labels))
    ):
        raise ArtifactError("source build recipe files must be non-empty, unique and sorted")
    digest = hashlib.sha256()
    records: list[dict[str, Any]] = []
    for label in labels:
        path = checked_workspace_file(workspace, label)
        size = path.stat().st_size
        digest_field(digest, label)
        digest_field(digest, str(size))
        file_digest = hashlib.sha256()
        with path.open("rb") as stream:
            while chunk := stream.read(1024 * 1024):
                digest.update(chunk)
                file_digest.update(chunk)
        digest.update(b"\0")
        records.append({"path": label, "bytes": size, "sha256": file_digest.hexdigest()})
    return {
        "algorithm": RECIPE_ALGORITHM,
        "sha256": digest.hexdigest(),
        "files": records,
    }


def digest_field(digest: "hashlib._Hash", value: str) -> None:
    encoded = value.encode("utf-8")
    digest.update(str(len(encoded)).encode("ascii"))
    digest.update(b":")
    digest.update(encoded)
    digest.update(b"\0")


def artifact_exclusions(lock: dict) -> dict[str, str]:
    result: dict[str, str] = {}
    for item in lock.get("runtime_artifact", {}).get("excluded_symlinks", []):
        path = PurePosixPath(item.get("path", ""))
        target = item.get("target", "")
        if path.is_absolute() or ".." in path.parts or not path.parts or not target.startswith("/"):
            raise ArtifactError(f"invalid runtime artifact exclusion: {item!r}")
        result[(PurePosixPath("usr") / path).as_posix()] = target
    return result


def load_raw_lock(path: Path) -> dict:
    try:
        lock = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ArtifactError(f"cannot read Python lock for runtime packing: {exc}") from exc
    if lock.get("schema_version") != 1:
        raise ArtifactError("unsupported Python lock for runtime packing")
    return lock


def local_runtime_entries(root: Path, exclusions: dict[str, str] | None = None):
    exclusions = exclusions or {}
    paths = [root]
    paths.extend(sorted(root.rglob("*"), key=lambda path: path.relative_to(root).as_posix()))
    for path in paths:
        relative = Path("usr") if path == root else Path("usr") / path.relative_to(root)
        metadata = path.lstat()
        mode = stat.S_IMODE(metadata.st_mode)
        if stat.S_ISDIR(metadata.st_mode):
            yield relative.as_posix(), "dir", mode, None, path
        elif stat.S_ISREG(metadata.st_mode):
            yield relative.as_posix(), "file", mode, None, path
        elif stat.S_ISLNK(metadata.st_mode):
            target = os.readlink(path)
            expected_excluded_target = exclusions.get(relative.as_posix())
            if expected_excluded_target is not None:
                if target.replace("\\", "/") != expected_excluded_target:
                    raise ArtifactError(
                        f"excluded runtime symlink target changed: {relative} -> {target}"
                    )
                continue
            pure_target = PurePosixPath(target.replace("\\", "/"))
            if pure_target.is_absolute():
                raise ArtifactError(f"absolute runtime symlink is forbidden: {relative} -> {target}")
            combined = PurePosixPath(relative.as_posix()).parent.joinpath(pure_target)
            normalized: list[str] = []
            for part in combined.parts:
                if part == "..":
                    if not normalized:
                        raise ArtifactError(f"runtime symlink escapes artifact: {relative} -> {target}")
                    normalized.pop()
                elif part not in ("", "."):
                    normalized.append(part)
            if not normalized or normalized[0] != "usr":
                raise ArtifactError(f"runtime symlink escapes usr: {relative} -> {target}")
            yield relative.as_posix(), "symlink", mode, target.replace("\\", "/"), path
        else:
            raise ArtifactError(f"unsupported runtime filesystem entry: {path}")


def local_tree_digest(
    root: Path, exclusions: dict[str, str] | None = None
) -> tuple[str, int, int]:
    digest = hashlib.sha256()
    count = 0
    total = 0
    for name, kind, mode, link, path in local_runtime_entries(root, exclusions):
        digest_field(digest, name)
        digest_field(digest, kind)
        digest_field(digest, f"{mode:o}")
        if kind == "file":
            size = path.stat().st_size
            digest_field(digest, str(size))
            with path.open("rb") as stream:
                while chunk := stream.read(1024 * 1024):
                    digest.update(chunk)
            digest.update(b"\0")
            total += size
        elif kind == "symlink":
            digest_field(digest, link or "")
        count += 1
    return digest.hexdigest(), count, total


def write_deterministic_archive(
    root: Path, destination: Path, exclusions: dict[str, str] | None = None
) -> None:
    temporary = destination.with_name(f".{destination.name}.{os.getpid()}.tmp")
    try:
        with temporary.open("xb") as raw:
            with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as compressed:
                with tarfile.open(fileobj=compressed, mode="w|", format=tarfile.PAX_FORMAT) as archive:
                    for name, kind, mode, link, path in local_runtime_entries(root, exclusions):
                        info = tarfile.TarInfo(name=name)
                        info.uid = 0
                        info.gid = 0
                        info.uname = ""
                        info.gname = ""
                        info.mtime = 0
                        info.mode = mode
                        if kind == "dir":
                            info.type = tarfile.DIRTYPE
                            info.size = 0
                            archive.addfile(info)
                        elif kind == "symlink":
                            info.type = tarfile.SYMTYPE
                            info.linkname = link or ""
                            info.size = 0
                            archive.addfile(info)
                        else:
                            info.type = tarfile.REGTYPE
                            info.size = path.stat().st_size
                            with path.open("rb") as stream:
                                archive.addfile(info, stream)
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)


def validate_member(member: tarfile.TarInfo) -> PurePosixPath:
    path = PurePosixPath(member.name)
    if path.is_absolute() or ".." in path.parts or not path.parts or path.parts[0] != "usr":
        raise ArtifactError(f"unsafe runtime archive member: {member.name!r}")
    if member.ischr() or member.isblk() or member.isfifo() or member.isdev():
        raise ArtifactError(f"special runtime archive entry is forbidden: {member.name}")
    if member.issym():
        target = PurePosixPath(member.linkname)
        if target.is_absolute():
            raise ArtifactError(f"absolute runtime archive symlink is forbidden: {member.name}")
        combined = path.parent.joinpath(target)
        normalized: list[str] = []
        for part in combined.parts:
            if part == "..":
                if not normalized:
                    raise ArtifactError(f"runtime archive symlink escapes: {member.name}")
                normalized.pop()
            elif part not in ("", "."):
                normalized.append(part)
        if not normalized or normalized[0] != "usr":
            raise ArtifactError(f"runtime archive symlink escapes usr: {member.name}")
    if member.islnk():
        raise ArtifactError(f"hard links are forbidden in runtime artifact: {member.name}")
    return path


def archive_tree_digest(archive_path: Path) -> tuple[str, int, int, dict[str, str]]:
    digest = hashlib.sha256()
    count = 0
    total = 0
    critical_hashes: dict[str, str] = {}
    seen: set[str] = set()
    seen_casefold: set[str] = set()
    with tarfile.open(archive_path, "r:gz") as archive:
        for member in archive:
            path = validate_member(member)
            name = path.as_posix()
            if name in seen or name.casefold() in seen_casefold:
                raise ArtifactError(f"duplicate/case-colliding runtime archive member: {name}")
            seen.add(name)
            seen_casefold.add(name.casefold())
            if member.isdir():
                kind = "dir"
            elif member.isfile():
                kind = "file"
            elif member.issym():
                kind = "symlink"
            else:
                raise ArtifactError(f"unsupported runtime archive member: {name}")
            digest_field(digest, name)
            digest_field(digest, kind)
            digest_field(digest, f"{member.mode:o}")
            if kind == "file":
                digest_field(digest, str(member.size))
                stream = archive.extractfile(member)
                if stream is None:
                    raise ArtifactError(f"cannot read runtime archive member: {name}")
                file_digest = hashlib.sha256()
                while chunk := stream.read(1024 * 1024):
                    digest.update(chunk)
                    file_digest.update(chunk)
                digest.update(b"\0")
                critical_hashes[name] = file_digest.hexdigest()
                total += member.size
            elif kind == "symlink":
                digest_field(digest, member.linkname)
            count += 1
    return digest.hexdigest(), count, total, critical_hashes


def archive_candidate_names(logical_path: str) -> list[str]:
    names = ["usr/" + logical_path]
    if logical_path.startswith("lib/_sysconfigdata_") or logical_path.startswith(
        "lib/config-3.12-aarch64-linux-ohos/"
    ):
        names.append("usr/lib/python3.12/" + remove_prefix(logical_path, "lib/"))
    return names


def resolve_archive_regular(
    members: dict[str, tarfile.TarInfo], candidate: str
) -> tarfile.TarInfo | None:
    current = PurePosixPath(candidate)
    visited: set[str] = set()
    for _ in range(32):
        name = current.as_posix()
        if name in visited:
            raise ArtifactError(f"runtime artifact symlink cycle: {candidate}")
        visited.add(name)
        member = members.get(name)
        if member is None:
            return None
        if member.isfile():
            return member
        if not member.issym():
            return None
        target = PurePosixPath(member.linkname)
        if target.is_absolute():
            raise ArtifactError(f"absolute runtime artifact symlink is forbidden: {name}")
        combined = current.parent.joinpath(target)
        normalized: list[str] = []
        for part in combined.parts:
            if part == "..":
                if not normalized:
                    raise ArtifactError(f"runtime artifact symlink escapes: {name}")
                normalized.pop()
            elif part not in ("", "."):
                normalized.append(part)
        if not normalized or normalized[0] != "usr":
            raise ArtifactError(f"runtime artifact symlink escapes usr: {name}")
        current = PurePosixPath(*normalized)
    raise ArtifactError(f"runtime artifact symlink chain is too deep: {candidate}")


def verify_elf_header(header: bytes, name: str) -> None:
    if len(header) < 20 or header[:4] != b"\x7fELF":
        raise ArtifactError(f"runtime artifact target binary is not ELF: {name}")
    if header[4] != 2 or header[5] != 1:
        raise ArtifactError(f"runtime artifact target binary is not little-endian ELF64: {name}")
    if int.from_bytes(header[18:20], "little") != 183:
        raise ArtifactError(f"runtime artifact target binary is not AArch64: {name}")


def elf_dynamic_identity(data: bytes, name: str) -> dict[str, Any]:
    """Read the ELF64 dynamic string tags needed by the release contract."""
    verify_elf_header(data[:20], name)
    if len(data) < 64:
        raise ArtifactError(f"truncated ELF header: {name}")
    program_offset = struct.unpack_from("<Q", data, 32)[0]
    program_size = struct.unpack_from("<H", data, 54)[0]
    program_count = struct.unpack_from("<H", data, 56)[0]
    if program_size < 56 or program_count < 1:
        raise ArtifactError(f"ELF has no usable program headers: {name}")
    segments: list[tuple[int, int, int, int, int]] = []
    dynamic: tuple[int, int] | None = None
    for index in range(program_count):
        offset = program_offset + index * program_size
        if offset + 56 > len(data):
            raise ArtifactError(f"truncated ELF program header: {name}")
        kind, _, file_offset, virtual, _, file_size, memory_size, _ = struct.unpack_from(
            "<IIQQQQQQ", data, offset
        )
        segments.append((kind, file_offset, virtual, file_size, memory_size))
        if kind == 2:  # PT_DYNAMIC
            dynamic = (file_offset, file_size)
    if dynamic is None:
        return {"needed": [], "soname": None, "rpath": None, "runpath": None}
    dynamic_offset, dynamic_size = dynamic
    if dynamic_offset + dynamic_size > len(data):
        raise ArtifactError(f"truncated ELF dynamic section: {name}")
    tags: list[tuple[int, int]] = []
    for offset in range(dynamic_offset, dynamic_offset + dynamic_size, 16):
        tag, value = struct.unpack_from("<qQ", data, offset)
        if tag == 0:
            break
        tags.append((tag, value))
    string_vaddr = next((value for tag, value in tags if tag == 5), None)
    string_size = next((value for tag, value in tags if tag == 10), None)
    if string_vaddr is None or string_size is None:
        if any(tag in {1, 14, 15, 29} for tag, _ in tags):
            raise ArtifactError(f"ELF dynamic strings cannot be located: {name}")
        return {"needed": [], "soname": None, "rpath": None, "runpath": None}
    string_offset = None
    for kind, file_offset, virtual, file_size, memory_size in segments:
        if kind == 1 and virtual <= string_vaddr < virtual + memory_size:  # PT_LOAD
            candidate = file_offset + (string_vaddr - virtual)
            if candidate + string_size <= file_offset + file_size:
                string_offset = candidate
                break
    if string_offset is None or string_offset + string_size > len(data):
        raise ArtifactError(f"ELF dynamic string table is outside the file: {name}")
    strings = data[string_offset : string_offset + string_size]

    def dynamic_string(index: int) -> str:
        if index >= len(strings):
            raise ArtifactError(f"ELF dynamic string index is invalid: {name}")
        end = strings.find(b"\0", index)
        if end < 0:
            raise ArtifactError(f"ELF dynamic string is unterminated: {name}")
        try:
            return strings[index:end].decode("utf-8")
        except UnicodeDecodeError as exc:
            raise ArtifactError(f"ELF dynamic string is not UTF-8: {name}") from exc

    needed = [dynamic_string(value) for tag, value in tags if tag == 1]
    scalar: dict[int, str | None] = {}
    for requested in (14, 15, 29):
        values = [dynamic_string(value) for tag, value in tags if tag == requested]
        if len(values) > 1:
            raise ArtifactError(f"duplicate ELF dynamic tag {requested}: {name}")
        scalar[requested] = values[0] if values else None
    return {
        "needed": needed,
        "soname": scalar[14],
        "rpath": scalar[15],
        "runpath": scalar[29],
    }


def read_archive_regular(archive: tarfile.TarFile, member: tarfile.TarInfo) -> bytes:
    stream = archive.extractfile(member)
    if stream is None:
        raise ArtifactError(f"cannot read runtime archive member: {member.name}")
    return stream.read()


def inspect_source_runtime_archive(
    archive_path: Path, source_lock: dict[str, Any]
) -> dict[str, Any]:
    required = source_lock.get("required_stdlib_extensions")
    if (
        not isinstance(required, list)
        or required != sorted(set(required))
        or any(not isinstance(item, str) for item in required)
    ):
        raise ArtifactError("source lock required extension list is invalid")
    dso_contract = source_lock.get("bundled_dsos")
    if not isinstance(dso_contract, list) or not dso_contract:
        raise ArtifactError("source lock bundled DSO contract is missing")

    with tarfile.open(archive_path, "r:gz") as archive:
        members = {member.name: member for member in archive.getmembers()}
        pyc = [name for name in members if name.endswith(".pyc")]
        if pyc:
            raise ArtifactError(f"runtime archive contains bytecode cache: {pyc[0]}")
        dynamic_prefix = "usr/lib/python3.12/lib-dynload/"
        extension_members = sorted(
            (
                member
                for name, member in members.items()
                if name.startswith(dynamic_prefix) and name.endswith(".so") and member.isfile()
            ),
            key=lambda item: item.name,
        )
        module_members: dict[str, tarfile.TarInfo] = {}
        for member in extension_members:
            basename = PurePosixPath(member.name).name
            module = basename.split(".cpython-312-", 1)[0]
            if module in module_members:
                raise ArtifactError(f"duplicate runtime extension module: {module}")
            module_members[module] = member
        missing = [name for name in required if name not in module_members]
        if missing:
            raise ArtifactError("required runtime extension is missing: " + ", ".join(missing))
        if len(extension_members) < 70:
            raise ArtifactError(
                f"runtime has only {len(extension_members)} dynamic extensions; expected at least 70"
            )

        elf_count = 0
        module_records: list[dict[str, Any]] = []
        all_dynamic: dict[str, dict[str, Any]] = {}
        for member in sorted(
            (item for item in members.values() if item.isfile()), key=lambda item: item.name
        ):
            stream = archive.extractfile(member)
            if stream is None:
                raise ArtifactError(f"cannot read runtime archive member: {member.name}")
            header = stream.read(20)
            if header[:4] != b"\x7fELF":
                continue
            remainder = stream.read()
            data = header + remainder
            identity = elf_dynamic_identity(data, member.name)
            if identity["rpath"] is not None or identity["runpath"] is not None:
                raise ArtifactError(f"RPATH/RUNPATH is forbidden in runtime ELF: {member.name}")
            all_dynamic[member.name] = identity
            elf_count += 1
        for module in required:
            member = module_members[module]
            data = read_archive_regular(archive, member)
            identity = all_dynamic[member.name]
            module_records.append(
                {
                    "name": module,
                    "path": remove_prefix(member.name, "usr/"),
                    "sha256": hashlib.sha256(data).hexdigest(),
                    "needed": identity["needed"],
                }
            )

        dso_records: list[dict[str, Any]] = []
        for contract in dso_contract:
            if not isinstance(contract, dict):
                raise ArtifactError("invalid bundled DSO contract entry")
            logical = contract.get("path")
            soname = contract.get("soname")
            if not isinstance(logical, str) or not isinstance(soname, str):
                raise ArtifactError("invalid bundled DSO path/SONAME")
            archive_name = "usr/" + logical
            member = members.get(archive_name)
            if member is None or not member.isfile():
                raise ArtifactError(f"bundled DSO is missing: {logical}")
            data = read_archive_regular(archive, member)
            identity = all_dynamic.get(archive_name) or elf_dynamic_identity(data, archive_name)
            if identity["soname"] != soname:
                raise ArtifactError(
                    f"bundled DSO SONAME mismatch for {logical}: {identity['soname']!r}"
                )
            dso_records.append(
                {
                    "path": logical,
                    "sha256": hashlib.sha256(data).hexdigest(),
                    "soname": soname,
                    "needed": identity["needed"],
                }
            )

        link_contract = {
            "lib/libbz2.so": "libbz2.so.1",
            "lib/libbz2.so.1": "libbz2.so.1.0",
            "lib/libbz2.so.1.0": "libbz2.so.1.0.8",
            "lib/libcrypto.so": "libcrypto.so.3",
            "lib/libffi.so": "libffi.so.8.3.1",
            "lib/libffi.so.8": "libffi.so.8.3.1",
            "lib/liblzma.so": "liblzma.so.5.8.3",
            "lib/liblzma.so.5": "liblzma.so.5.8.3",
            "lib/libsqlite3.so": "libsqlite3.so.0",
            "lib/libsqlite3.so.0": "libsqlite3.so.0.8.6",
            "lib/libssl.so": "libssl.so.3",
        }
        for logical, target in link_contract.items():
            member = members.get("usr/" + logical)
            if member is None or not member.issym() or member.linkname != target:
                raise ArtifactError(
                    f"runtime SONAME link mismatch: {logical} -> {getattr(member, 'linkname', None)!r}"
                )

        text_checks = [
            "usr/bin/python3.12-config",
            "usr/bin/pydoc3.12",
            "usr/lib/pkgconfig/python-3.12.pc",
            "usr/lib/pkgconfig/python-3.12-embed.pc",
        ]
        for name in text_checks:
            member = members.get(name)
            if member is None or not member.isfile():
                raise ArtifactError(f"runtime operational metadata is missing: {name}")
            data = read_archive_regular(archive, member)
            if b"/var/tmp/" in data or b"/tmp/" in data:
                raise ArtifactError(f"ephemeral operational path remains in {name}")
        pyconfig = members.get("usr/include/python3.12/pyconfig.h")
        if pyconfig is None or b"#define ENABLE_IPV6 1" not in read_archive_regular(
            archive, pyconfig
        ):
            raise ArtifactError("runtime was not built with IPv6 enabled")
        for name in (
            "usr/etc/ssl/openssl.cnf",
            "usr/etc/ssl/cert.pem",
            "usr/etc/ssl/certs",
            "usr/lib/ossl-modules",
        ):
            if name not in members:
                raise ArtifactError(f"runtime-owned OpenSSL path is missing: {name}")

    return {
        "dynamic_extension_count": len(extension_members),
        "required_extensions": module_records,
        "bundled_dsos": dso_records,
        "elf_file_count": elf_count,
        "policy": {
            "all_elf_aarch64": True,
            "no_rpath_runpath": True,
            "no_ephemeral_operational_paths": True,
            "no_pyc": True,
            "ipv6_enabled": True,
            "openssl_isolated_paths_present": True,
            "production_prefix": "/data/python312-rk3588a/usr",
        },
    }


def verify_archive_interface(
    archive_path: Path, lock: dict, file_hashes: dict[str, str]
) -> dict[str, object]:
    with tarfile.open(archive_path, "r:gz") as archive:
        members = {member.name: member for member in archive.getmembers()}

        observed: list[dict[str, object]] = []
        for item in lock["runtime_interface"]["files"]:
            candidates = archive_candidate_names(item["path"])
            resolved = next(
                (
                    (candidate, member)
                    for candidate in candidates
                    if (member := resolve_archive_regular(members, candidate)) is not None
                ),
                None,
            )
            if resolved is None:
                raise ArtifactError(
                    "runtime artifact interface file is missing: " + " or ".join(candidates)
                )
            logical_name, member = resolved
            actual = file_hashes[member.name]
            if actual != item["sha256"]:
                raise ArtifactError(
                    f"runtime artifact interface mismatch for {logical_name}: "
                    f"expected {item['sha256']}, got {actual}"
                )
            observed.append(
                {"path": item["path"], "sha256": actual, "size": member.size}
            )

        include_prefix = "usr/include/python3.12/"
        include_members = sorted(
            (
                member
                for name, member in members.items()
                if name.startswith(include_prefix) and member.isfile()
            ),
            key=lambda member: remove_prefix(member.name, include_prefix),
        )
        include_digest = hashlib.sha256()
        include_bytes = 0
        for member in include_members:
            relative = remove_prefix(member.name, include_prefix).encode("utf-8")
            include_digest.update(relative)
            include_digest.update(b"\0")
            include_digest.update(str(member.size).encode("ascii"))
            include_digest.update(b"\0")
            stream = archive.extractfile(member)
            if stream is None:
                raise ArtifactError(f"cannot read runtime header: {member.name}")
            while chunk := stream.read(1024 * 1024):
                include_digest.update(chunk)
            include_digest.update(b"\0")
            include_bytes += member.size
        include_sha = include_digest.hexdigest()
        expected_include = lock["runtime_interface"]["include_tree_sha256"]
        if include_sha != expected_include:
            raise ArtifactError(
                f"runtime artifact include-tree mismatch: expected {expected_include}, got {include_sha}"
            )

        def read_member(candidate_names: list[str]) -> tuple[str, bytes]:
            for candidate in candidate_names:
                member = resolve_archive_regular(members, candidate)
                if member is not None:
                    stream = archive.extractfile(member)
                    if stream is None:
                        break
                    return candidate, stream.read()
            raise ArtifactError("runtime artifact file is missing: " + " or ".join(candidate_names))

        patch_name, patchlevel = read_member(
            archive_candidate_names("include/python3.12/patchlevel.h")
        )
        expected_version = lock["target"]["python_version"]
        if f'#define PY_VERSION              "{expected_version}"'.encode() not in patchlevel:
            raise ArtifactError(f"runtime artifact {patch_name} is not CPython {expected_version}")

        sysconfig_name, sysconfig = read_member(
            archive_candidate_names("lib/_sysconfigdata__linux_aarch64-linux-ohos.py")
        )
        try:
            sysconfig_tree = ast.parse(sysconfig.decode("utf-8"), filename=sysconfig_name)
            sysconfig_vars = next(
                ast.literal_eval(node.value)
                for node in sysconfig_tree.body
                if isinstance(node, ast.Assign)
                and any(
                    isinstance(target, ast.Name) and target.id == "build_time_vars"
                    for target in node.targets
                )
            )
        except (UnicodeDecodeError, SyntaxError, ValueError, StopIteration) as exc:
            raise ArtifactError(
                f"runtime artifact {sysconfig_name} has no literal build_time_vars mapping"
            ) from exc
        if not isinstance(sysconfig_vars, dict):
            raise ArtifactError(
                f"runtime artifact {sysconfig_name} build_time_vars is not a mapping"
            )
        for key, expected in (
            ("SOABI", lock["target"]["soabi"]),
            ("EXT_SUFFIX", lock["target"]["extension_suffix"]),
            ("MULTIARCH", "aarch64-linux-ohos"),
        ):
            if sysconfig_vars.get(key) != expected:
                raise ArtifactError(
                    f"runtime artifact {sysconfig_name} does not declare {key}={expected}"
                )

        for logical in (
            "bin/python3.12",
            "lib/libpython3.12.so.1.0",
            "lib/libffi.so.8",
        ):
            name, data = read_member(archive_candidate_names(logical))
            verify_elf_header(data[:20], name)
        read_member(archive_candidate_names("lib/python3.12/os.py"))

    return {
        "python_version": lock["target"]["python_version"],
        "soabi": lock["target"]["soabi"],
        "include_tree_sha256": include_sha,
        "include_file_count": len(include_members),
        "include_bytes": include_bytes,
        "files": observed,
    }


def derive_runtime_interface(runtime: Path, target: dict[str, Any]) -> dict[str, Any]:
    include_root = runtime / "include" / "python3.12"
    if not include_root.is_dir():
        raise ArtifactError(f"runtime include tree is missing: {include_root}")
    digest = hashlib.sha256()
    include_count = 0
    include_bytes = 0
    for path in sorted(
        (item for item in include_root.rglob("*") if item.is_file() and not item.is_symlink()),
        key=lambda item: item.relative_to(include_root).as_posix(),
    ):
        relative = path.relative_to(include_root).as_posix().encode("utf-8")
        size = path.stat().st_size
        digest.update(relative)
        digest.update(b"\0")
        digest.update(str(size).encode("ascii"))
        digest.update(b"\0")
        with path.open("rb") as stream:
            while chunk := stream.read(1024 * 1024):
                digest.update(chunk)
        digest.update(b"\0")
        include_count += 1
        include_bytes += size
    logical_paths = [
        "include/python3.12/Python.h",
        "include/python3.12/patchlevel.h",
        "lib/libpython3.12.so.1.0",
        "lib/libffi.so.8",
        "lib/_sysconfigdata__linux_aarch64-linux-ohos.py",
        "lib/config-3.12-aarch64-linux-ohos/Makefile",
    ]
    files: list[dict[str, Any]] = []
    for logical in logical_paths:
        candidates = [runtime.joinpath(*PurePosixPath(logical).parts)]
        if logical.startswith("lib/_sysconfigdata_") or logical.startswith("lib/config-3.12-"):
            candidates.append(runtime / "lib" / "python3.12" / remove_prefix(logical, "lib/"))
        path = next((item for item in candidates if item.is_file()), None)
        if path is None:
            raise ArtifactError(f"source-built runtime interface file is missing: {logical}")
        files.append({"path": logical, "sha256": sha256_file(path)})
    interface = {"include_tree_sha256": digest.hexdigest(), "files": files}
    return {
        "contract": interface,
        "sha256": canonical_json_sha256(interface),
        "include_file_count": include_count,
        "include_bytes": include_bytes,
        "python_version": target.get("python_version"),
        "soabi": target.get("soabi"),
    }


def sha256sum_lines_tree(root: Path) -> str:
    if not root.is_dir():
        raise ArtifactError(f"tree input is missing: {root}")
    digest = hashlib.sha256()
    regular = sorted(
        (item for item in root.rglob("*") if item.is_file() and not item.is_symlink()),
        key=lambda item: item.relative_to(root).as_posix().encode("utf-8"),
    )
    links = sorted(
        (item for item in root.rglob("*") if item.is_symlink()),
        key=lambda item: item.relative_to(root).as_posix().encode("utf-8"),
    )
    for path in regular:
        relative = "./" + path.relative_to(root).as_posix()
        digest.update(f"{sha256_file(path)}  {relative}\n".encode("utf-8"))
    for path in links:
        relative = "./" + path.relative_to(root).as_posix()
        digest.update(f"L {relative} -> {os.readlink(path)}\n".encode("utf-8"))
    return digest.hexdigest()


def parse_stdlib_extension_matrix(configure_log: Path) -> list[dict[str, str]]:
    if configure_log.is_symlink() or not configure_log.is_file():
        raise ArtifactError(f"target configure log is missing: {configure_log}")
    pattern = re.compile(r"^checking for stdlib extension module ([^ .]+)\.\.\. (.+)$")
    statuses: dict[str, str] = {}
    for line in configure_log.read_text(encoding="utf-8", errors="strict").splitlines():
        match = pattern.match(line)
        if not match:
            continue
        name, status = match.groups()
        if name in statuses:
            raise ArtifactError(f"duplicate stdlib configure outcome: {name}")
        statuses[name] = status
    # CPython's configure reports the conditionally probed subset here; core
    # extensions such as select/_posixsubprocess are built without this probe.
    if len(statuses) < 40:
        raise ArtifactError(
            f"target configure log contains only {len(statuses)} stdlib outcomes"
        )
    return [
        {"name": name, "status": statuses[name], "evidence": "configure"}
        for name in sorted(statuses)
    ]


def dependency_output_records(prefix: Path, source_lock: dict[str, Any]) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    for item in source_lock.get("bundled_dsos", []):
        path = prefix.joinpath(*PurePosixPath(item["path"]).parts)
        if path.is_symlink() or not path.is_file():
            raise ArtifactError(f"dependency output DSO is missing: {path}")
        data = path.read_bytes()
        identity = elf_dynamic_identity(data, str(path))
        if identity["soname"] != item["soname"]:
            raise ArtifactError(f"dependency output SONAME mismatch: {path}")
        records.append(
            {
                "path": item["path"],
                "sha256": hashlib.sha256(data).hexdigest(),
                "soname": identity["soname"],
                "needed": identity["needed"],
            }
        )
    return records


def create_source_receipt_payload(
    *,
    source_lock_path: Path,
    runtime: Path,
    archive: Path,
    build_dir: Path,
    prepared_source: Path,
    host_python: Path,
    toolchain: Path,
    sysroot: Path,
    deps_prefix: Path,
) -> dict[str, Any]:
    source_lock = load_json_object(source_lock_path, "Python source input lock")
    if (
        type(source_lock.get("schema_version")) is not int
        or source_lock.get("schema_version") != 1
        or source_lock.get("kind") != "cpython-ohos-source-input-lock"
    ):
        raise ArtifactError("unsupported Python source input lock")
    workspace = source_lock_path.resolve().parents[2]
    recipe = build_recipe_record(workspace, source_lock)
    target = source_lock.get("target")
    if not isinstance(target, dict):
        raise ArtifactError("Python source input lock target is missing")

    expected_source_tree = source_lock.get("inputs", {}).get("prepared_source", {}).get(
        "tree_sha256"
    )
    actual_source_tree = sha256sum_lines_tree(prepared_source)
    if actual_source_tree != expected_source_tree:
        raise ArtifactError(
            f"prepared CPython source tree mismatch: expected {expected_source_tree}, "
            f"got {actual_source_tree}"
        )
    expected_sysroot_tree = source_lock.get("inputs", {}).get("ohos_native_sdk", {}).get(
        "sysroot_tree_sha256"
    )
    actual_sysroot_tree = sha256sum_lines_tree(sysroot)
    if actual_sysroot_tree != expected_sysroot_tree:
        raise ArtifactError(
            f"OHOS SDK sysroot tree mismatch: expected {expected_sysroot_tree}, "
            f"got {actual_sysroot_tree}"
        )
    clang = toolchain / "bin" / "clang"
    readelf = toolchain / "bin" / "llvm-readelf"
    clang_lock = source_lock.get("inputs", {}).get("ohos_clang", {})
    if sha256_file(clang) != clang_lock.get("clang_sha256"):
        raise ArtifactError("OHOS clang binary differs from source input lock")
    if sha256_file(readelf) != clang_lock.get("llvm_readelf_sha256"):
        raise ArtifactError("OHOS llvm-readelf differs from source input lock")

    exclusions: dict[str, str] = {}
    runtime_tree, runtime_entries, runtime_bytes = local_tree_digest(runtime, exclusions)
    archive_tree, archive_entries, archive_bytes, _ = archive_tree_digest(archive)
    if (runtime_tree, runtime_entries, runtime_bytes) != (
        archive_tree,
        archive_entries,
        archive_bytes,
    ):
        raise ArtifactError("source runtime tree differs from its deterministic archive")
    interface = derive_runtime_interface(runtime, target)
    inspection = inspect_source_runtime_archive(archive, source_lock)
    matrix = parse_stdlib_extension_matrix(build_dir / "configure.log")
    outcomes = {item["name"]: item["status"] for item in matrix}
    for required in source_lock["required_stdlib_extensions"]:
        if required not in outcomes:
            matrix.append(
                {
                    "name": required,
                    "status": "built-unconditionally",
                    "evidence": "archive",
                }
            )
        elif outcomes[required] != "yes":
            raise ArtifactError(
                f"required stdlib extension was not enabled by configure: {required}="
                f"{outcomes.get(required)!r}"
            )
    matrix.sort(key=lambda item: item["name"])
    libpython = next(
        item
        for item in interface["contract"]["files"]
        if item["path"] == "lib/libpython3.12.so.1.0"
    )
    return {
        "schema_version": 1,
        "kind": SOURCE_RECEIPT_KIND,
        "complete": True,
        "provenance_mode": SOURCE_MODE,
        "target": target,
        "python_source_lock_sha256": sha256_file(source_lock_path),
        "build_recipe_sha256": recipe["sha256"],
        "python_source_lock": {
            "path": "scripts/python_source/source_build.lock.json",
            "sha256": sha256_file(source_lock_path),
        },
        "build_recipe": recipe,
        "inputs": source_lock["inputs"],
        "observed_derived_inputs": {
            "prepared_source_tree_sha256": actual_source_tree,
            "sysroot_tree_sha256": actual_sysroot_tree,
            "host_python_sha256": sha256_file(host_python),
            "clang_sha256": sha256_file(clang),
            "llvm_readelf_sha256": sha256_file(readelf),
            "dependency_outputs": dependency_output_records(deps_prefix, source_lock),
        },
        "stdlib_extensions": matrix,
        "stdlib_extension_summary": {
            status: sum(1 for item in matrix if item["status"] == status)
            for status in sorted({item["status"] for item in matrix})
        },
        "output": {
            "runtime_tree_sha256": runtime_tree,
            "runtime_entry_count": runtime_entries,
            "runtime_payload_bytes": runtime_bytes,
            "archive_filename": archive.name,
            "archive_sha256": sha256_file(archive),
            "archive_bytes": archive.stat().st_size,
            "runtime_interface": interface["contract"],
            "runtime_interface_sha256": interface["sha256"],
            "libpython_sha256": libpython["sha256"],
            "inspection": inspection,
        },
    }


def verify_source_receipt(
    manifest_path: Path, artifact: dict[str, Any], lock_path: Path
) -> dict[str, Any]:
    """Verify the hash-bound source receipt and its actual runtime archive.

    This is intentionally a pure callable API for the ROS build-receipt layer.
    It performs no writes and derives trust anchors from the selected runtime
    lock and repository files, not from receipt self-assertions.
    """
    manifest_path = Path(manifest_path)
    lock_path = Path(lock_path)
    manifest_on_disk = load_json_object(manifest_path, "Python runtime artifact manifest")
    if manifest_on_disk != artifact:
        raise ArtifactError("caller artifact object differs from manifest bytes")
    if artifact.get("provenance_mode") != SOURCE_MODE:
        raise ArtifactError("source receipt verification requires source-reproducible mode")
    manager = load_manager()
    lock = manager.load_lock(lock_path)
    workspace = workspace_for_python_lock(lock_path)
    binding = lock.get("source_build")
    if not isinstance(binding, dict):
        raise ArtifactError("Python runtime lock lacks source_build trust anchors")
    lock_source = binding.get("source_lock")
    if not isinstance(lock_source, dict) or not isinstance(lock_source.get("path"), str):
        raise ArtifactError("Python runtime lock source_lock binding is invalid")
    source_lock_path = checked_workspace_file(workspace, lock_source["path"])
    expected_source_lock_sha = require_sha256(
        lock_source.get("sha256"), "Python runtime lock source lock hash"
    )
    actual_source_lock_sha = sha256_file(source_lock_path)
    if actual_source_lock_sha != expected_source_lock_sha:
        raise ArtifactError("repository Python source input lock differs from runtime lock")
    source_lock = load_json_object(source_lock_path, "Python source input lock")
    if source_lock.get("target") != lock.get("target"):
        raise ArtifactError("Python source input lock target differs from runtime lock")
    recipe = build_recipe_record(workspace, source_lock)
    expected_recipe_sha = require_sha256(
        binding.get("build_recipe_sha256"), "Python runtime lock recipe hash"
    )
    if recipe["sha256"] != expected_recipe_sha:
        raise ArtifactError("repository Python source recipe differs from runtime lock")

    receipt_binding = artifact.get("source_build_receipt")
    lock_receipt = binding.get("source_build_receipt")
    if not isinstance(receipt_binding, dict) or receipt_binding != lock_receipt:
        raise ArtifactError("artifact source receipt binding differs from runtime lock")
    filename = receipt_binding.get("filename")
    if filename != SOURCE_RECEIPT_NAME or PurePosixPath(filename).name != filename:
        raise ArtifactError("source receipt must use the fixed safe sibling filename")
    receipt_path = manifest_path.parent / filename
    receipt_sha = sha256_file(receipt_path) if receipt_path.is_file() and not receipt_path.is_symlink() else None
    expected_receipt_sha = require_sha256(
        receipt_binding.get("sha256"), "source build receipt hash"
    )
    if receipt_sha != expected_receipt_sha:
        raise ArtifactError("source build receipt bytes differ from manifest/runtime lock")
    receipt = load_json_object(receipt_path, "Python source build receipt")
    if (
        type(receipt.get("schema_version")) is not int
        or receipt.get("schema_version") != 1
        or receipt.get("kind") != SOURCE_RECEIPT_KIND
        or receipt.get("complete") is not True
        or receipt.get("provenance_mode") != SOURCE_MODE
    ):
        raise ArtifactError("source build receipt identity/completion is invalid")
    if receipt.get("target") != lock.get("target"):
        raise ArtifactError("source build receipt target differs from runtime lock")
    if receipt.get("python_source_lock_sha256") != actual_source_lock_sha:
        raise ArtifactError("source build receipt source lock hash mismatch")
    if receipt.get("build_recipe_sha256") != recipe["sha256"]:
        raise ArtifactError("source build receipt recipe hash mismatch")
    if receipt.get("python_source_lock") != {
        "path": lock_source["path"],
        "sha256": actual_source_lock_sha,
    }:
        raise ArtifactError("source build receipt source lock record mismatch")
    if receipt.get("build_recipe") != recipe:
        raise ArtifactError("source build receipt recipe inventory mismatch")
    if receipt.get("inputs") != source_lock.get("inputs"):
        raise ArtifactError("source build receipt fixed inputs differ from source lock")

    archive_record = artifact.get("archive")
    if not isinstance(archive_record, dict) or not isinstance(archive_record.get("filename"), str):
        raise ArtifactError("runtime artifact archive record is invalid")
    archive_name = archive_record["filename"]
    if PurePosixPath(archive_name).name != archive_name:
        raise ArtifactError("runtime archive must be a safe sibling filename")
    archive_path = manifest_path.parent / archive_name
    if archive_path.is_symlink() or not archive_path.is_file():
        raise ArtifactError("source runtime archive is missing or a symlink")
    actual_archive_sha = sha256_file(archive_path)
    if actual_archive_sha != require_sha256(archive_record.get("sha256"), "archive hash"):
        raise ArtifactError("source runtime archive hash mismatch")
    if require_positive_int(archive_record.get("bytes"), "archive bytes") != archive_path.stat().st_size:
        raise ArtifactError("source runtime archive byte count mismatch")
    tree_sha, entry_count, payload_bytes, file_hashes = archive_tree_digest(archive_path)
    if require_positive_int(
        artifact.get("runtime_entry_count"), "artifact runtime entry count"
    ) != entry_count:
        raise ArtifactError("artifact manifest runtime entry count mismatch")
    if require_positive_int(
        artifact.get("runtime_payload_bytes"), "artifact runtime payload bytes"
    ) != payload_bytes:
        raise ArtifactError("artifact manifest runtime payload byte count mismatch")
    interface = verify_archive_interface(archive_path, lock, file_hashes)
    inspection = inspect_source_runtime_archive(archive_path, source_lock)
    output = receipt.get("output")
    if not isinstance(output, dict):
        raise ArtifactError("source build receipt output is missing")
    expected_output = {
        "runtime_tree_sha256": tree_sha,
        "runtime_entry_count": entry_count,
        "runtime_payload_bytes": payload_bytes,
        "archive_filename": archive_name,
        "archive_sha256": actual_archive_sha,
        "archive_bytes": archive_path.stat().st_size,
        "runtime_interface": lock["runtime_interface"],
        "runtime_interface_sha256": canonical_json_sha256(lock["runtime_interface"]),
        "libpython_sha256": next(
            item["sha256"]
            for item in lock["runtime_interface"]["files"]
            if item["path"] == "lib/libpython3.12.so.1.0"
        ),
        "inspection": inspection,
    }
    if output != expected_output:
        raise ArtifactError("source build receipt output differs from actual archive/runtime lock")
    if artifact.get("runtime_tree_sha256") != tree_sha:
        raise ArtifactError("artifact manifest tree differs from source receipt/archive")
    if artifact.get("runtime_entry_count") != entry_count or artifact.get(
        "runtime_payload_bytes"
    ) != payload_bytes:
        raise ArtifactError("artifact manifest inventory differs from source receipt/archive")
    if artifact.get("runtime_interface") != interface:
        raise ArtifactError("artifact manifest interface differs from source receipt/archive")
    matrix = receipt.get("stdlib_extensions")
    if not isinstance(matrix, list) or not matrix:
        raise ArtifactError("source build receipt lacks stdlib extension matrix")
    matrix_names = [item.get("name") for item in matrix if isinstance(item, dict)]
    if matrix_names != sorted(set(matrix_names)) or len(matrix_names) != len(matrix):
        raise ArtifactError("source build receipt stdlib matrix is invalid")
    statuses = {item["name"]: item.get("status") for item in matrix}
    for required in source_lock["required_stdlib_extensions"]:
        if statuses.get(required) not in {"yes", "built-unconditionally"}:
            raise ArtifactError(f"required stdlib extension is not enabled: {required}")
    return {
        "path": str(receipt_path.resolve()),
        "sha256": expected_receipt_sha,
        "receipt": receipt,
    }


def manifest_path_for(archive: Path) -> Path:
    return archive.with_name(archive.name + ".manifest.json")


def cmd_create_source_receipt(args: argparse.Namespace) -> None:
    output = Path(args.output).resolve()
    if output.name != SOURCE_RECEIPT_NAME:
        raise ArtifactError(f"source receipt filename must be {SOURCE_RECEIPT_NAME}")
    if output.exists():
        raise ArtifactError(f"create-only source build receipt already exists: {output}")
    payload = create_source_receipt_payload(
        source_lock_path=Path(args.source_lock).resolve(),
        runtime=Path(args.runtime_usr).resolve(),
        archive=Path(args.archive).resolve(),
        build_dir=Path(args.build_dir).resolve(),
        prepared_source=Path(args.prepared_source).resolve(),
        host_python=Path(args.host_python).resolve(),
        toolchain=Path(args.toolchain).resolve(),
        sysroot=Path(args.sysroot).resolve(),
        deps_prefix=Path(args.deps_prefix).resolve(),
    )
    temporary = output.with_name(f".{output.name}.{os.getpid()}.tmp")
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    os.replace(temporary, output)
    print(json.dumps({"path": str(output), "sha256": sha256_file(output)}, sort_keys=True))


def cmd_create(args: argparse.Namespace) -> None:
    manager = load_manager()
    lock_path = Path(args.lock).resolve()
    lock = manager.load_lock(lock_path)
    exclusions = artifact_exclusions(lock)
    runtime = Path(args.runtime_usr).resolve()
    output = Path(args.output).resolve()
    manifest = manifest_path_for(output)
    if output.exists() or manifest.exists():
        raise ArtifactError(f"create-only output already exists: {output} or {manifest}")
    for required in ("bin/python3.12", "lib/libpython3.12.so.1.0", "lib/python3.12/os.py"):
        if not runtime.joinpath(*PurePosixPath(required).parts).exists():
            raise ArtifactError(f"full runtime input is missing {required}: {runtime}")
    interface = manager.verify_runtime(runtime, lock)
    tree_sha, entry_count, payload_bytes = local_tree_digest(runtime, exclusions)
    output.parent.mkdir(parents=True, exist_ok=True)
    write_deterministic_archive(runtime, output, exclusions)
    archive_sha = sha256_file(output)
    archive_tree_sha, archive_entries, archive_bytes, _ = archive_tree_digest(output)
    if (archive_tree_sha, archive_entries, archive_bytes) != (tree_sha, entry_count, payload_bytes):
        output.unlink(missing_ok=True)
        raise ArtifactError("deterministic archive semantic digest does not match source runtime")
    payload = {
        "schema_version": 1,
        "generated_at_utc": utc_now(),
        "provenance_mode": ARTIFACT_MODE,
        "source_gap": (
            "Jiusi-pys/python main at the pinned commit consumes an existing base libpython seed; "
            "this manifest binds the complete accepted runtime artifact but does not claim a clean "
            "source-to-libpython reproduction."
        ),
        "origin": args.origin,
        "python_lock_sha256": manager.lock_sha256(lock_path),
        "source": lock["source"],
        "target": lock["target"],
        "runtime_interface": interface,
        "runtime_artifact_exclusions": lock.get("runtime_artifact", {}),
        "runtime_tree_sha256": tree_sha,
        "runtime_entry_count": entry_count,
        "runtime_payload_bytes": payload_bytes,
        "archive": {
            "filename": output.name,
            "format": "deterministic-tar-gzip",
            "sha256": archive_sha,
            "bytes": output.stat().st_size,
        },
    }
    temporary = manifest.with_name(f".{manifest.name}.{os.getpid()}.tmp")
    temporary.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    os.replace(temporary, manifest)
    print(json.dumps(payload, sort_keys=True))


def cmd_pack(args: argparse.Namespace) -> None:
    runtime = Path(args.runtime_usr).resolve()
    output = Path(args.output).resolve()
    lock = load_raw_lock(Path(args.lock).resolve())
    exclusions = artifact_exclusions(lock)
    if output.exists():
        raise ArtifactError(f"create-only output already exists: {output}")
    for required in ("bin/python3.12", "lib/libpython3.12.so.1.0", "lib/python3.12/os.py"):
        if not runtime.joinpath(*PurePosixPath(required).parts).exists():
            raise ArtifactError(f"full runtime input is missing {required}: {runtime}")
    tree_sha, entry_count, payload_bytes = local_tree_digest(runtime, exclusions)
    output.parent.mkdir(parents=True, exist_ok=True)
    write_deterministic_archive(runtime, output, exclusions)
    archive_sha = sha256_file(output)
    archived_tree, archived_entries, archived_bytes, _ = archive_tree_digest(output)
    if (archived_tree, archived_entries, archived_bytes) != (
        tree_sha,
        entry_count,
        payload_bytes,
    ):
        output.unlink(missing_ok=True)
        raise ArtifactError("packed runtime semantic digest does not match its source tree")
    print(
        json.dumps(
            {
                "archive_sha256": archive_sha,
                "runtime_tree_sha256": tree_sha,
                "runtime_entry_count": entry_count,
                "runtime_payload_bytes": payload_bytes,
            },
            sort_keys=True,
        )
    )


def cmd_tree(args: argparse.Namespace) -> None:
    runtime = Path(args.runtime_usr).resolve()
    tree_sha, entry_count, payload_bytes = local_tree_digest(runtime)
    if args.expect_sha256 and tree_sha != args.expect_sha256:
        raise ArtifactError(
            f"runtime tree mismatch: expected {args.expect_sha256}, got {tree_sha}"
        )
    if args.expect_entries is not None and entry_count != args.expect_entries:
        raise ArtifactError(
            f"runtime entry count mismatch: expected {args.expect_entries}, got {entry_count}"
        )
    if args.expect_bytes is not None and payload_bytes != args.expect_bytes:
        raise ArtifactError(
            f"runtime payload size mismatch: expected {args.expect_bytes}, got {payload_bytes}"
        )
    print(
        json.dumps(
            {
                "runtime_tree_sha256": tree_sha,
                "runtime_entry_count": entry_count,
                "runtime_payload_bytes": payload_bytes,
            },
            sort_keys=True,
        )
    )


def cmd_extract(args: argparse.Namespace) -> None:
    archive_path = Path(args.archive).resolve()
    destination = Path(args.destination).resolve()
    if destination.exists():
        raise ArtifactError(f"create-only extraction destination already exists: {destination}")
    tree_sha, entry_count, payload_bytes, _ = archive_tree_digest(archive_path)
    destination.mkdir(parents=True)
    try:
        with tarfile.open(archive_path, "r:gz") as archive:
            members = archive.getmembers()
            for member in members:
                validate_member(member)
            archive.extractall(destination, members=members, filter="fully_trusted")
    except Exception:
        shutil.rmtree(destination, ignore_errors=True)
        raise
    extracted_sha, extracted_entries, extracted_bytes = local_tree_digest(destination / "usr")
    if (extracted_sha, extracted_entries, extracted_bytes) != (
        tree_sha,
        entry_count,
        payload_bytes,
    ):
        shutil.rmtree(destination, ignore_errors=True)
        raise ArtifactError("extracted runtime semantic tree does not match its archive")
    print(
        json.dumps(
            {
                "runtime_tree_sha256": tree_sha,
                "runtime_entry_count": entry_count,
                "runtime_payload_bytes": payload_bytes,
            },
            sort_keys=True,
        )
    )


def cmd_deployment_marker(args: argparse.Namespace) -> None:
    if not REMOTE_RUNTIME_PREFIX.fullmatch(args.remote_prefix):
        raise ArtifactError(
            "remote runtime prefix must be an isolated hash-suffixed /data path: "
            f"{args.remote_prefix!r}"
        )
    manager = load_manager()
    lock_path = Path(args.lock).resolve()
    lock = manager.load_lock(lock_path)
    manifest_path = Path(args.artifact_manifest).resolve()
    artifact = load_json_object(manifest_path, "runtime artifact manifest")
    if artifact.get("python_lock_sha256") != manager.lock_sha256(lock_path):
        raise ArtifactError("runtime artifact deployment marker lock mismatch")
    if artifact.get("target") != lock["target"]:
        raise ArtifactError("runtime artifact deployment marker target mismatch")
    mode = artifact.get("provenance_mode")
    if mode == SOURCE_MODE:
        verified_source = verify_source_receipt(manifest_path, artifact, lock_path)
        source_receipt = verified_source["receipt"]
        source_fields = {
            "python_source_build_receipt_sha256": verified_source["sha256"],
            "python_source_lock_sha256": source_receipt["python_source_lock_sha256"],
            "python_source_build_recipe_sha256": source_receipt["build_recipe_sha256"],
        }
    elif mode == ARTIFACT_MODE:
        if "source_build_receipt" in artifact:
            raise ArtifactError("artifact-only manifest must not carry a source receipt")
        source_fields = {
            "python_source_build_receipt_sha256": "NOT_APPLICABLE",
            "python_source_lock_sha256": "NOT_APPLICABLE",
            "python_source_build_recipe_sha256": "NOT_APPLICABLE",
        }
    else:
        raise ArtifactError(f"unsupported runtime provenance mode: {mode!r}")
    payload = {
        "schema_version": 1,
        "complete": True,
        "generated_at_utc": utc_now(),
        "board_serial": args.board,
        "remote_runtime_prefix": args.remote_prefix,
        "python_lock_sha256": artifact["python_lock_sha256"],
        "python_runtime_archive_sha256": artifact["archive"]["sha256"],
        "python_runtime_tree_sha256": artifact["runtime_tree_sha256"],
        "python_runtime_entry_count": artifact["runtime_entry_count"],
        "python_runtime_payload_bytes": artifact["runtime_payload_bytes"],
        "runtime_provenance_mode": mode,
        "source": artifact["source"],
        "target": artifact["target"],
        **source_fields,
    }
    output = Path(args.output).resolve()
    if output.exists():
        raise ArtifactError(f"create-only runtime deployment marker already exists: {output}")
    temporary = output.with_name(f".{output.name}.{os.getpid()}.tmp")
    temporary.parent.mkdir(parents=True, exist_ok=True)
    temporary.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    os.replace(temporary, output)
    print(json.dumps(payload, sort_keys=True))


def cmd_seal(args: argparse.Namespace) -> None:
    manager = load_manager()
    lock_path = Path(args.lock).resolve()
    lock = manager.load_lock(lock_path)
    archive = Path(args.archive).resolve()
    manifest = manifest_path_for(archive)
    if manifest.exists():
        raise ArtifactError(f"create-only runtime artifact manifest already exists: {manifest}")
    tree_sha, entry_count, payload_bytes, file_hashes = archive_tree_digest(archive)
    interface = verify_archive_interface(archive, lock, file_hashes)
    source_receipt_path = (
        Path(args.source_build_receipt).resolve() if args.source_build_receipt else None
    )
    if source_receipt_path is not None:
        if source_receipt_path.parent != archive.parent or source_receipt_path.name != SOURCE_RECEIPT_NAME:
            raise ArtifactError(
                f"source receipt must be the {SOURCE_RECEIPT_NAME} sibling of the archive"
            )
        source_record = {
            "filename": SOURCE_RECEIPT_NAME,
            "sha256": sha256_file(source_receipt_path),
        }
        if lock.get("source_build", {}).get("source_build_receipt") != source_record:
            raise ArtifactError("source receipt is not pinned by the Python runtime lock")
        provenance_mode = SOURCE_MODE
    else:
        source_record = None
        provenance_mode = ARTIFACT_MODE
    payload = {
        "schema_version": 1,
        "generated_at_utc": utc_now(),
        "provenance_mode": provenance_mode,
        "origin": args.origin,
        "python_lock_sha256": manager.lock_sha256(lock_path),
        "source": lock["source"],
        "target": lock["target"],
        "runtime_interface": interface,
        "runtime_artifact_exclusions": lock.get("runtime_artifact", {}),
        "runtime_tree_sha256": tree_sha,
        "runtime_entry_count": entry_count,
        "runtime_payload_bytes": payload_bytes,
        "archive": {
            "filename": archive.name,
            "format": "deterministic-tar-gzip",
            "sha256": sha256_file(archive),
            "bytes": archive.stat().st_size,
        },
    }
    if source_record is None:
        payload["source_gap"] = (
            "Jiusi-pys/python main at the pinned commit consumes an existing base libpython seed; "
            "this manifest binds the complete accepted runtime artifact but does not claim a clean "
            "source-to-libpython reproduction."
        )
    else:
        payload["source_build_receipt"] = source_record
    temporary = manifest.with_name(f".{manifest.name}.{os.getpid()}.tmp")
    temporary.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    os.replace(temporary, manifest)
    if source_record is not None:
        try:
            verify_source_receipt(manifest, payload, lock_path)
        except Exception:
            manifest.unlink(missing_ok=True)
            raise
    print(json.dumps(payload, sort_keys=True))


def cmd_verify(args: argparse.Namespace) -> None:
    manager = load_manager()
    lock_path = Path(args.lock).resolve()
    lock = manager.load_lock(lock_path)
    archive = Path(args.archive).resolve()
    manifest = Path(args.manifest).resolve() if args.manifest else manifest_path_for(archive)
    payload = load_json_object(manifest, "runtime artifact manifest")
    if payload.get("schema_version") != 1:
        raise ArtifactError("unsupported runtime artifact manifest")
    if payload.get("python_lock_sha256") != manager.lock_sha256(lock_path):
        raise ArtifactError("runtime artifact was made from a different Python lock")
    if payload.get("target") != lock["target"]:
        raise ArtifactError("runtime artifact target does not match the Python lock")
    if payload.get("runtime_artifact_exclusions") != lock.get("runtime_artifact", {}):
        raise ArtifactError("runtime artifact exclusion policy does not match the Python lock")
    expected_archive = payload.get("archive", {})
    if expected_archive.get("filename") != archive.name:
        raise ArtifactError("runtime archive filename does not match its manifest")
    actual_archive_sha = sha256_file(archive)
    if actual_archive_sha != expected_archive.get("sha256"):
        raise ArtifactError(
            f"runtime archive hash mismatch: expected {expected_archive.get('sha256')}, got {actual_archive_sha}"
        )
    if require_positive_int(expected_archive.get("bytes"), "runtime archive bytes") != archive.stat().st_size:
        raise ArtifactError("runtime archive byte count does not match its manifest")
    tree_sha, entry_count, payload_bytes, file_hashes = archive_tree_digest(archive)
    if tree_sha != payload.get("runtime_tree_sha256"):
        raise ArtifactError("runtime archive semantic tree hash mismatch")
    if entry_count != require_positive_int(
        payload.get("runtime_entry_count"), "runtime entry count"
    ) or payload_bytes != require_positive_int(
        payload.get("runtime_payload_bytes"), "runtime payload bytes"
    ):
        raise ArtifactError("runtime archive inventory count/size mismatch")
    interface = verify_archive_interface(archive, lock, file_hashes)
    if payload.get("runtime_interface") != interface:
        raise ArtifactError("runtime archive interface record does not match the archive")
    mode = payload.get("provenance_mode")
    if mode == SOURCE_MODE:
        verify_source_receipt(manifest, payload, lock_path)
    elif mode == ARTIFACT_MODE:
        if "source_build_receipt" in payload:
            raise ArtifactError("artifact-only manifest must not carry a source receipt")
    else:
        raise ArtifactError(f"unsupported runtime provenance mode: {mode!r}")
    print(
        f"python_runtime_artifact_verified sha256={actual_archive_sha} "
        f"tree_sha256={tree_sha} entries={entry_count}"
    )


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lock", default=str(DEFAULT_LOCK))
    subparsers = parser.add_subparsers(dest="command", required=True)
    create = subparsers.add_parser("create")
    create.add_argument("--runtime-usr", required=True)
    create.add_argument("--output", required=True)
    create.add_argument("--origin", required=True)
    create.set_defaults(handler=cmd_create)
    pack = subparsers.add_parser("pack")
    pack.add_argument("--runtime-usr", required=True)
    pack.add_argument("--output", required=True)
    pack.set_defaults(handler=cmd_pack)
    tree = subparsers.add_parser("tree")
    tree.add_argument("--runtime-usr", required=True)
    tree.add_argument("--expect-sha256")
    tree.add_argument("--expect-entries", type=int)
    tree.add_argument("--expect-bytes", type=int)
    tree.set_defaults(handler=cmd_tree)
    extract = subparsers.add_parser("extract")
    extract.add_argument("--archive", required=True)
    extract.add_argument("--destination", required=True)
    extract.set_defaults(handler=cmd_extract)
    source_receipt = subparsers.add_parser("create-source-receipt")
    source_receipt.add_argument("--source-lock", required=True)
    source_receipt.add_argument("--runtime-usr", required=True)
    source_receipt.add_argument("--archive", required=True)
    source_receipt.add_argument("--build-dir", required=True)
    source_receipt.add_argument("--prepared-source", required=True)
    source_receipt.add_argument("--host-python", required=True)
    source_receipt.add_argument("--toolchain", required=True)
    source_receipt.add_argument("--sysroot", required=True)
    source_receipt.add_argument("--deps-prefix", required=True)
    source_receipt.add_argument("--output", required=True)
    source_receipt.set_defaults(handler=cmd_create_source_receipt)
    seal = subparsers.add_parser("seal")
    seal.add_argument("--archive", required=True)
    seal.add_argument("--origin", required=True)
    seal.add_argument("--source-build-receipt")
    seal.set_defaults(handler=cmd_seal)
    deployment = subparsers.add_parser("create-deployment-marker")
    deployment.add_argument("--artifact-manifest", required=True)
    deployment.add_argument("--output", required=True)
    deployment.add_argument("--board", required=True)
    deployment.add_argument("--remote-prefix", required=True)
    deployment.set_defaults(handler=cmd_deployment_marker)
    verify = subparsers.add_parser("verify")
    verify.add_argument("--archive", required=True)
    verify.add_argument("--manifest")
    verify.set_defaults(handler=cmd_verify)
    return parser


def main() -> int:
    args = make_parser().parse_args()
    try:
        args.handler(args)
    except (ArtifactError, OSError, tarfile.TarError) as exc:
        print(f"Python runtime artifact failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
