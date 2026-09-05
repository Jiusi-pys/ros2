#!/usr/bin/env python3
"""Reproducible Python 3.12 inputs for the RK3588A/OpenHarmony ROS 2 port."""

from __future__ import annotations

import argparse
import ast
import datetime as dt
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import zipfile


WORKSPACE = Path(__file__).resolve().parent.parent
DEFAULT_LOCK = WORKSPACE / "scripts" / "python" / "ohos_python.lock.json"
STAGE_MARKER = ".ros2-ohos-python-stage.json"
DEPLOYMENT_MARKER = ".ros2-ohos-python-deployment.json"
OVERLAY_FILE_MANIFEST = ".ros2-ohos-python-files.sha256"
OVERLAY_PATH_INVENTORY = ".ros2-ohos-python-paths.txt"
RUNTIME_MARKER = "PYTHON_TARGET_PROVENANCE.json"
SAFE_ENTRY = re.compile(r"^[A-Za-z0-9_.+][A-Za-z0-9_.+-]*$")
HEX_SHA256 = re.compile(r"^[0-9a-f]{64}$")
REMOTE_RUNTIME_PREFIX = re.compile(r"^/data/[A-Za-z0-9._+-]+$")


class ContractError(RuntimeError):
    pass


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def tree_digest(root: Path) -> tuple[str, int, int]:
    if root.is_file():
        return sha256_file(root), 1, root.stat().st_size
    if not root.is_dir():
        raise ContractError(f"tree root does not exist: {root}")
    digest = hashlib.sha256()
    count = 0
    size_total = 0
    files = sorted(
        (path for path in root.rglob("*") if path.is_file()),
        key=lambda path: path.relative_to(root).as_posix(),
    )
    for path in files:
        relative = path.relative_to(root).as_posix().encode("utf-8")
        size = path.stat().st_size
        digest.update(relative)
        digest.update(b"\0")
        digest.update(str(size).encode("ascii"))
        digest.update(b"\0")
        with path.open("rb") as stream:
            while chunk := stream.read(1024 * 1024):
                digest.update(chunk)
        digest.update(b"\0")
        count += 1
        size_total += size
    return digest.hexdigest(), count, size_total


def load_lock(path: Path) -> dict:
    try:
        raw = path.read_bytes()
        data = json.loads(raw)
    except (OSError, json.JSONDecodeError) as exc:
        raise ContractError(f"cannot read Python lock {path}: {exc}") from exc
    if data.get("schema_version") != 1:
        raise ContractError("unsupported Python lock schema")
    target = data.get("target", {})
    expected_target = {
        "architecture": "aarch64",
        "platform": "linux-ohos",
        "python_api_version": "3.12",
        "soabi": "cpython-312-aarch64-linux-ohos",
        "extension_suffix": ".cpython-312-aarch64-linux-ohos.so",
    }
    for key, expected in expected_target.items():
        if target.get(key) != expected:
            raise ContractError(f"unexpected target.{key}: {target.get(key)!r}")
    for section in ("wheels", "sdists"):
        names: set[str] = set()
        for item in data.get(section, []):
            filename = item.get("filename", "")
            digest = item.get("sha256", "")
            if not filename or filename in names:
                raise ContractError(f"invalid or duplicate {section} filename: {filename!r}")
            if not HEX_SHA256.fullmatch(digest):
                raise ContractError(f"invalid SHA-256 for {filename}")
            names.add(filename)
    return data


def lock_sha256(path: Path) -> str:
    return sha256_file(path)


def verify_elf_aarch64(path: Path) -> None:
    try:
        header = path.read_bytes()[:20]
    except OSError as exc:
        raise ContractError(f"cannot read ELF {path}: {exc}") from exc
    if len(header) < 20 or header[:4] != b"\x7fELF":
        raise ContractError(f"target binary is not ELF: {path}")
    if header[4] != 2 or header[5] != 1:
        raise ContractError(f"target binary is not little-endian ELF64: {path}")
    if struct.unpack_from("<H", header, 18)[0] != 183:
        raise ContractError(f"target binary is not AArch64: {path}")


def runtime_candidate_paths(root: Path, relative: PurePosixPath) -> list[Path]:
    candidates = [root.joinpath(*relative.parts)]
    # The ROS build interface is intentionally flattened, while a complete
    # CPython installation keeps these generated files below lib/python3.12.
    if relative.parts and relative.parts[0] == "lib" and (
        relative.name.startswith("_sysconfigdata_") or relative.parts[1:2] == ("config-3.12-aarch64-linux-ohos",)
    ):
        candidates.append(root / "lib" / "python3.12" / Path(*relative.parts[1:]))
    return candidates


def verify_runtime(root: Path, lock: dict) -> dict:
    root = root.resolve()
    interface = lock["runtime_interface"]
    observed: list[dict[str, object]] = []
    for item in interface["files"]:
        relative = PurePosixPath(item["path"])
        if relative.is_absolute() or ".." in relative.parts:
            raise ContractError(f"unsafe runtime interface path: {relative}")
        candidates = runtime_candidate_paths(root, relative)
        path = next((candidate for candidate in candidates if candidate.is_file()), candidates[0])
        if not path.is_file():
            raise ContractError(
                "required target Python file is missing: "
                + " or ".join(str(candidate) for candidate in candidates)
            )
        actual = sha256_file(path)
        if actual != item["sha256"]:
            raise ContractError(
                f"target Python hash mismatch for {relative}: expected {item['sha256']}, got {actual}"
            )
        observed.append({"path": relative.as_posix(), "sha256": actual, "size": path.stat().st_size})

    include_root = root / "include" / "python3.12"
    include_hash, include_files, include_bytes = tree_digest(include_root)
    if include_hash != interface["include_tree_sha256"]:
        raise ContractError(
            "target Python include-tree hash mismatch: "
            f"expected {interface['include_tree_sha256']}, got {include_hash}"
        )

    patchlevel = (include_root / "patchlevel.h").read_text(encoding="utf-8", errors="strict")
    version = lock["target"]["python_version"]
    if f'#define PY_VERSION              "{version}"' not in patchlevel:
        raise ContractError(f"target headers are not CPython {version}")

    sysconfig_path = next(
        candidate
        for candidate in runtime_candidate_paths(
            root, PurePosixPath("lib/_sysconfigdata__linux_aarch64-linux-ohos.py")
        )
        if candidate.is_file()
    )
    try:
        sysconfig_tree = ast.parse(
            sysconfig_path.read_text(encoding="utf-8", errors="strict"),
            filename=str(sysconfig_path),
        )
        sysconfig = next(
            ast.literal_eval(node.value)
            for node in sysconfig_tree.body
            if isinstance(node, ast.Assign)
            and any(
                isinstance(target, ast.Name) and target.id == "build_time_vars"
                for target in node.targets
            )
        )
    except (SyntaxError, ValueError, StopIteration) as exc:
        raise ContractError("target sysconfig has no literal build_time_vars mapping") from exc
    if not isinstance(sysconfig, dict):
        raise ContractError("target sysconfig build_time_vars is not a mapping")
    for key, expected in (
        ("SOABI", lock["target"]["soabi"]),
        ("EXT_SUFFIX", lock["target"]["extension_suffix"]),
        ("MULTIARCH", "aarch64-linux-ohos"),
    ):
        if sysconfig.get(key) != expected:
            raise ContractError(f"target sysconfig does not declare {key}={expected}")

    verify_elf_aarch64(root / "lib" / "libpython3.12.so.1.0")
    verify_elf_aarch64(root / "lib" / "libffi.so.8")
    return {
        "python_version": version,
        "soabi": lock["target"]["soabi"],
        "include_tree_sha256": include_hash,
        "include_file_count": include_files,
        "include_bytes": include_bytes,
        "files": observed,
    }


def atomic_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    temporary.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    os.replace(temporary, path)


def cmd_verify_runtime(args: argparse.Namespace, lock: dict, lock_path: Path) -> None:
    root = Path(args.root)
    result = verify_runtime(root, lock)
    if args.write_provenance:
        payload = {
            "schema_version": 1,
            "generated_at_utc": utc_now(),
            "provenance_mode": "hash-bound-runtime-interface",
            "source_attestation": "not-asserted-by-pull; reproduce against the pinned source separately",
            "source": lock["source"],
            "lock_sha256": lock_sha256(lock_path),
            "origin": args.origin,
            "runtime_interface": result,
        }
        atomic_json(root / RUNTIME_MARKER, payload)
    print(json.dumps(result, sort_keys=True))


def request_json(url: str) -> dict:
    request = urllib.request.Request(url, headers={"User-Agent": "ros2-ohos-python-lock/1"})
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)


def release_url(item: dict) -> str:
    project = item["project"]
    version = item["version"]
    metadata = request_json(f"https://pypi.org/pypi/{project}/{version}/json")
    candidates = [entry for entry in metadata.get("urls", []) if entry.get("filename") == item["filename"]]
    if len(candidates) != 1:
        raise ContractError(f"PyPI has {len(candidates)} matches for {item['filename']}")
    published_hash = candidates[0].get("digests", {}).get("sha256")
    if published_hash != item["sha256"]:
        raise ContractError(
            f"PyPI digest changed for {item['filename']}: expected {item['sha256']}, got {published_hash}"
        )
    return candidates[0]["url"]


def download_locked(item: dict, destination: Path) -> None:
    if destination.exists():
        actual = sha256_file(destination)
        if actual != item["sha256"]:
            raise ContractError(
                f"refusing to overwrite mismatched artifact {destination}: expected {item['sha256']}, got {actual}"
            )
        print(f"verified {destination.name}")
        return
    url = release_url(item)
    destination.parent.mkdir(parents=True, exist_ok=True)
    part = destination.with_name(f".{destination.name}.{os.getpid()}.part")
    try:
        request = urllib.request.Request(url, headers={"User-Agent": "ros2-ohos-python-lock/1"})
        with urllib.request.urlopen(request, timeout=60) as response, part.open("wb") as output:
            shutil.copyfileobj(response, output, length=1024 * 1024)
        actual = sha256_file(part)
        if actual != item["sha256"]:
            raise ContractError(
                f"download hash mismatch for {destination.name}: expected {item['sha256']}, got {actual}"
            )
        os.replace(part, destination)
        print(f"downloaded {destination.name}")
    finally:
        part.unlink(missing_ok=True)


def verify_artifact_set(directory: Path, items: list[dict], pattern: str) -> None:
    expected = {item["filename"]: item for item in items}
    actual = {path.name: path for path in directory.glob(pattern) if path.is_file()}
    missing = sorted(set(expected) - set(actual))
    extra = sorted(set(actual) - set(expected))
    if missing or extra:
        raise ContractError(f"artifact set mismatch in {directory}: missing={missing}, extra={extra}")
    for name, item in expected.items():
        digest = sha256_file(actual[name])
        if digest != item["sha256"]:
            raise ContractError(f"hash mismatch for {name}: expected {item['sha256']}, got {digest}")


def cmd_fetch_artifacts(args: argparse.Namespace, lock: dict, _lock_path: Path) -> None:
    wheels_dir = Path(args.wheels_dir)
    sources_dir = Path(args.sources_dir)
    if args.clean:
        for path in (wheels_dir, sources_dir):
            if path.exists():
                resolved = path.resolve()
                required_parent = (WORKSPACE / "python_target").resolve()
                if resolved.parent != required_parent:
                    raise ContractError(f"refusing to clean outside python_target: {resolved}")
                shutil.rmtree(resolved)
    for item in lock["wheels"]:
        download_locked(item, wheels_dir / item["filename"])
    for item in lock["sdists"]:
        download_locked(item, sources_dir / item["filename"])
    verify_artifact_set(wheels_dir, lock["wheels"], "*.whl")
    verify_artifact_set(sources_dir, lock["sdists"], "*.tar.*")


def validate_wheel_contract(item: dict) -> None:
    filename = item["filename"].lower()
    if any(token in filename for token in ("win32", "win_amd64", "x86_64", "i686")):
        raise ContractError(f"host/foreign wheel is forbidden: {item['filename']}")
    if item["architecture"] == "any":
        if not filename.endswith("-none-any.whl"):
            raise ContractError(f"pure wheel lacks none-any tag: {item['filename']}")
    elif item["architecture"] == "aarch64-musllinux":
        if "musllinux" not in filename or not filename.endswith("_aarch64.whl"):
            raise ContractError(f"binary wheel is not musllinux AArch64: {item['filename']}")
    else:
        raise ContractError(f"unknown wheel architecture: {item['architecture']}")


def safe_member_path(name: str) -> PurePosixPath:
    path = PurePosixPath(name)
    if path.is_absolute() or not path.parts or ".." in path.parts:
        raise ContractError(f"unsafe archive member: {name!r}")
    return path


def extract_wheel(wheel: Path, output: Path) -> None:
    with zipfile.ZipFile(wheel) as archive:
        for info in archive.infolist():
            relative = safe_member_path(info.filename)
            mode = info.external_attr >> 16
            if mode & 0o170000 == 0o120000:
                raise ContractError(f"wheel symlink is forbidden: {wheel.name}:{info.filename}")
            destination = output.joinpath(*relative.parts)
            if info.is_dir():
                destination.mkdir(parents=True, exist_ok=True)
                continue
            destination.parent.mkdir(parents=True, exist_ok=True)
            data = archive.read(info)
            if destination.exists() and destination.read_bytes() != data:
                raise ContractError(f"conflicting wheel member: {relative}")
            destination.write_bytes(data)


def stage_empy(archive_path: Path, output: Path) -> None:
    with tarfile.open(archive_path, "r:*") as archive:
        members = {member.name: member for member in archive.getmembers() if member.isfile()}
        em_name = next((name for name in members if name.endswith("/em.py")), None)
        meta_name = next((name for name in members if name.endswith("/PKG-INFO")), None)
        if not em_name or not meta_name:
            raise ContractError("empy sdist lacks em.py or PKG-INFO")
        em_file = archive.extractfile(members[em_name])
        metadata = archive.extractfile(members[meta_name])
        if em_file is None or metadata is None:
            raise ContractError("cannot extract empy payload")
        (output / "em.py").write_bytes(em_file.read())
        dist_info = output / "empy-3.3.4.dist-info"
        dist_info.mkdir(parents=True, exist_ok=True)
        (dist_info / "METADATA").write_bytes(metadata.read())
        (dist_info / "WHEEL").write_text(
            "Wheel-Version: 1.0\nGenerator: ros2-ohos-python-lock\nRoot-Is-Purelib: true\n"
            "Tag: py3-none-any\n",
            encoding="utf-8",
        )


def normalize_extensions(output: Path, target_suffix: str) -> None:
    source_suffix = ".cpython-312-aarch64-linux-musl.so"
    for path in sorted(output.rglob("*")):
        lowered = path.name.lower()
        if path.is_file() and (lowered.endswith(".pyd") or "win_amd64" in lowered):
            raise ContractError(f"Windows extension leaked into target stage: {path}")
        if not path.is_file() or not lowered.endswith(".so"):
            continue
        verify_elf_aarch64(path)
        if path.name.endswith(source_suffix):
            destination = path.with_name(path.name[: -len(source_suffix)] + target_suffix)
            if destination.exists():
                raise ContractError(f"extension rename collision: {destination}")
            path.rename(destination)


def cmd_unpack_stage(args: argparse.Namespace, lock: dict, _lock_path: Path) -> None:
    output = Path(args.output)
    wheels_dir = Path(args.wheels_dir)
    sources_dir = Path(args.sources_dir)
    if output.exists() and any(output.iterdir()):
        raise ContractError(f"stage output must be empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    verify_artifact_set(wheels_dir, lock["wheels"], "*.whl")
    verify_artifact_set(sources_dir, lock["sdists"], "*.tar.*")
    for item in lock["wheels"]:
        validate_wheel_contract(item)
        extract_wheel(wheels_dir / item["filename"], output)
    empy = next(item for item in lock["sdists"] if item["project"] == "empy")
    stage_empy(sources_dir / empy["filename"], output)
    normalize_extensions(output, lock["target"]["extension_suffix"])
    print(f"wheel_stage_unpacked={output}")


REQUIRED_STAGE_PATHS = (
    "argcomplete/__init__.py",
    "catkin_pkg/__init__.py",
    "cffi/__init__.py",
    "colcon_core/__init__.py",
    "colcon_cmake/__init__.py",
    "colcon_ros/__init__.py",
    "colcon_test_result/__init__.py",
    "cryptography/__init__.py",
    "em.py",
    "lark/__init__.py",
    "lxml/__init__.py",
    "numpy/__init__.py",
    "numpy/core/include/numpy/_numpyconfig.h",
    "packaging/__init__.py",
    "pip/__init__.py",
    "psutil/__init__.py",
    "pycparser/__init__.py",
    "pytest/__init__.py",
    "setuptools/__init__.py",
    "yaml/__init__.py",
)


def validate_stage_payload(site: Path, lock: dict) -> None:
    for relative in REQUIRED_STAGE_PATHS:
        if not site.joinpath(*PurePosixPath(relative).parts).is_file():
            raise ContractError(f"required staged package file is missing: {relative}")
    numpy_binary = site / "numpy" / "core" / (
        "_multiarray_umath" + lock["target"]["extension_suffix"]
    )
    if not numpy_binary.is_file():
        raise ContractError(f"target NumPy extension is missing: {numpy_binary}")
    verify_elf_aarch64(numpy_binary)
    for module in ("_psutil_linux", "_psutil_posix"):
        binary = site / "psutil" / (module + lock["target"]["extension_suffix"])
        if not binary.is_file():
            raise ContractError(f"target psutil extension is missing: {binary}")
        verify_elf_aarch64(binary)
    normalize_extensions(site, lock["target"]["extension_suffix"])


def managed_entries(site: Path) -> list[dict[str, object]]:
    entries: list[dict[str, object]] = []
    for path in sorted(site.iterdir(), key=lambda value: value.name):
        if path.name == STAGE_MARKER:
            continue
        if path.name in (".", "..") or path.name.startswith("-") or not SAFE_ENTRY.fullmatch(path.name):
            raise ContractError(f"unsafe top-level stage entry: {path.name!r}")
        digest, file_count, byte_count = tree_digest(path)
        entries.append(
            {
                "name": path.name,
                "kind": "directory" if path.is_dir() else "file",
                "tree_sha256": digest,
                "file_count": file_count,
                "bytes": byte_count,
            }
        )
    if not entries:
        raise ContractError("Python stage is empty")
    return entries


def stage_tree_digest(entries: list[dict[str, object]]) -> str:
    """Bind the complete top-level inventory without volatile timestamps."""
    canonical = json.dumps(entries, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(canonical).hexdigest()


def cmd_finalize_stage(args: argparse.Namespace, lock: dict, lock_path: Path) -> None:
    site = Path(args.site)
    if not site.is_dir():
        raise ContractError(f"stage directory does not exist: {site}")
    validate_stage_payload(site, lock)
    entries = managed_entries(site)
    payload = {
        "schema_version": 1,
        "complete": True,
        "generated_at_utc": utc_now(),
        "lock_sha256": lock_sha256(lock_path),
        "target": lock["target"],
        "source": lock["source"],
        "wheels": [
            {
                key: item[key]
                for key in ("project", "version", "filename", "architecture", "sha256")
            }
            for item in lock["wheels"]
        ],
        "sdists": lock["sdists"],
        "numpy_include": "numpy/core/include",
        "managed_entries": entries,
        "stage_tree_sha256": stage_tree_digest(entries),
    }
    atomic_json(site / STAGE_MARKER, payload)
    verify_stage(site, lock, lock_path)
    print(f"python_stage_finalized={site}")


def verify_stage(site: Path, lock: dict, lock_path: Path) -> dict:
    marker = site / STAGE_MARKER
    if not marker.is_file():
        raise ContractError(f"stage provenance marker is missing: {marker}")
    try:
        payload = json.loads(marker.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ContractError(f"invalid stage provenance marker: {exc}") from exc
    if payload.get("schema_version") != 1 or payload.get("complete") is not True:
        raise ContractError("stage provenance is not a complete schema-v1 record")
    if payload.get("lock_sha256") != lock_sha256(lock_path):
        raise ContractError("stage was generated from a different Python lock")
    if payload.get("target") != lock["target"]:
        raise ContractError("stage target ABI does not match the Python lock")
    recorded = payload.get("managed_entries")
    if not isinstance(recorded, list) or not recorded:
        raise ContractError("stage managed-entry inventory is empty")
    expected_stage_tree = stage_tree_digest(recorded)
    if payload.get("stage_tree_sha256") != expected_stage_tree:
        raise ContractError("stage provenance inventory digest is missing or invalid")
    actual_names = sorted(path.name for path in site.iterdir() if path.name != STAGE_MARKER)
    recorded_names = sorted(item.get("name", "") for item in recorded)
    if actual_names != recorded_names:
        raise ContractError(
            f"stage top-level inventory changed: recorded={recorded_names}, actual={actual_names}"
        )
    for item in recorded:
        name = item["name"]
        if name in (".", "..") or name.startswith("-") or not SAFE_ENTRY.fullmatch(name):
            raise ContractError(f"unsafe managed entry in provenance: {name!r}")
        digest, file_count, byte_count = tree_digest(site / name)
        if (
            digest != item.get("tree_sha256")
            or file_count != item.get("file_count")
            or byte_count != item.get("bytes")
        ):
            raise ContractError(f"staged entry changed after finalization: {name}")
    validate_stage_payload(site, lock)
    return payload


def cmd_create_deployment_marker(args: argparse.Namespace, lock: dict, lock_path: Path) -> None:
    if not REMOTE_RUNTIME_PREFIX.fullmatch(args.remote_runtime_prefix):
        raise ContractError(
            "remote runtime prefix must be an absolute, single-component /data path: "
            f"{args.remote_runtime_prefix!r}"
        )
    expected_overlay = f"{args.remote_runtime_prefix}/ros2-site-packages"
    if args.remote_overlay != expected_overlay:
        raise ContractError(
            f"remote overlay must be {expected_overlay!r}, got {args.remote_overlay!r}"
        )
    site = Path(args.site).resolve()
    stage = verify_stage(site, lock, lock_path)
    runtime_artifact = None
    source_fields = {
        "python_source_build_receipt_sha256": "NOT_APPLICABLE",
        "python_source_lock_sha256": "NOT_APPLICABLE",
        "python_source_build_recipe_sha256": "NOT_APPLICABLE",
    }
    if args.runtime_artifact_manifest:
        artifact_path = Path(args.runtime_artifact_manifest).resolve()
        import python_runtime_artifact as runtime_artifact_manager

        runtime_artifact = runtime_artifact_manager.load_json_object(
            artifact_path, "runtime artifact manifest"
        )
        if runtime_artifact.get("python_lock_sha256") != lock_sha256(lock_path):
            raise ContractError("runtime artifact manifest was generated from a different Python lock")
        if runtime_artifact.get("target") != lock["target"]:
            raise ContractError("runtime artifact target does not match the Python lock")
        archive = runtime_artifact.get("archive", {})
        for field in ("sha256", "bytes", "filename"):
            if field not in archive:
                raise ContractError(f"runtime artifact manifest is missing archive.{field}")
        if not HEX_SHA256.fullmatch(str(runtime_artifact.get("runtime_tree_sha256", ""))):
            raise ContractError("runtime artifact manifest has an invalid runtime tree digest")
        mode = runtime_artifact.get("provenance_mode")
        if mode == runtime_artifact_manager.SOURCE_MODE:
            try:
                verified_source = runtime_artifact_manager.verify_source_receipt(
                    artifact_path, runtime_artifact, lock_path
                )
            except runtime_artifact_manager.ArtifactError as exc:
                raise ContractError(f"invalid source-built Python runtime: {exc}") from exc
            source_receipt = verified_source["receipt"]
            source_fields = {
                "python_source_build_receipt_sha256": verified_source["sha256"],
                "python_source_lock_sha256": source_receipt["python_source_lock_sha256"],
                "python_source_build_recipe_sha256": source_receipt["build_recipe_sha256"],
            }
        elif mode == runtime_artifact_manager.ARTIFACT_MODE:
            if "source_build_receipt" in runtime_artifact:
                raise ContractError("artifact-only runtime must not carry a source receipt")
        else:
            raise ContractError(f"unsupported runtime provenance mode: {mode!r}")

    stage_marker = site / STAGE_MARKER
    file_manifest = Path(args.file_manifest).resolve()
    path_inventory = Path(args.path_inventory).resolve()
    if not file_manifest.is_file() or not path_inventory.is_file():
        raise ContractError("overlay file manifest and path inventory must exist")
    payload = {
        "schema_version": 1,
        "complete": True,
        "generated_at_utc": utc_now(),
        "board_serial": args.board,
        "remote_runtime_prefix": args.remote_runtime_prefix,
        "remote_overlay": args.remote_overlay,
        "python_lock_sha256": lock_sha256(lock_path),
        "python_stage_marker_sha256": sha256_file(stage_marker),
        "python_stage_tree_sha256": stage["stage_tree_sha256"],
        "python_overlay_file_manifest_sha256": sha256_file(file_manifest),
        "python_overlay_path_inventory_sha256": sha256_file(path_inventory),
        "python_runtime_interface_sha256": hashlib.sha256(
            json.dumps(lock["runtime_interface"], sort_keys=True, separators=(",", ":")).encode(
                "utf-8"
            )
        ).hexdigest(),
        "python_runtime_archive_sha256": (
            runtime_artifact["archive"]["sha256"] if runtime_artifact else None
        ),
        "python_runtime_tree_sha256": (
            runtime_artifact["runtime_tree_sha256"] if runtime_artifact else None
        ),
        "runtime_provenance_mode": (
            runtime_artifact.get("provenance_mode")
            if runtime_artifact
            else "runtime-interface-only-no-release-artifact"
        ),
        "source": lock["source"],
        "target": lock["target"],
        **source_fields,
    }
    if args.require_runtime_artifact and runtime_artifact is None:
        raise ContractError("a hash-bound runtime artifact manifest is required for this deployment")
    atomic_json(Path(args.output).resolve(), payload)
    print(json.dumps(payload, sort_keys=True))


def cmd_create_overlay_manifests(args: argparse.Namespace, lock: dict, lock_path: Path) -> None:
    site = Path(args.site).resolve()
    verify_stage(site, lock, lock_path)
    file_manifest = Path(args.file_manifest).resolve()
    path_inventory = Path(args.path_inventory).resolve()
    if file_manifest == path_inventory:
        raise ContractError("overlay file manifest and path inventory must be different files")
    for output in (file_manifest, path_inventory):
        if output.exists():
            raise ContractError(f"create-only overlay manifest output already exists: {output}")

    paths: list[tuple[str, str, Path | None]] = []
    for path in sorted(site.rglob("*"), key=lambda value: value.relative_to(site).as_posix()):
        relative = path.relative_to(site).as_posix()
        if "\n" in relative or "\r" in relative or "\\" in relative:
            raise ContractError(f"unsupported overlay path: {relative!r}")
        if path.is_symlink():
            raise ContractError(f"overlay symlinks are forbidden: {relative}")
        if path.is_dir():
            paths.append(("d", relative, None))
        elif path.is_file():
            paths.append(("f", relative, path))
        else:
            raise ContractError(f"unsupported overlay filesystem entry: {relative}")

    virtual_files = (OVERLAY_FILE_MANIFEST, OVERLAY_PATH_INVENTORY, DEPLOYMENT_MARKER)
    existing = {relative for _, relative, _ in paths}
    for name in virtual_files:
        if name in existing:
            raise ContractError(f"reserved overlay manifest path is already present: {name}")
        paths.append(("f", name, None))
    # The board emits directories and files as separate find streams and then
    # byte-sorts the complete "type path" lines.
    paths.sort(key=lambda item: (item[0], item[1]))

    path_inventory.parent.mkdir(parents=True, exist_ok=True)
    path_inventory.write_text(
        "".join(f"{kind} {relative}\n" for kind, relative, _ in paths), encoding="utf-8", newline="\n"
    )

    checksum_files = [(relative, path) for kind, relative, path in paths if kind == "f" and path]
    checksum_files.append((OVERLAY_PATH_INVENTORY, path_inventory))
    checksum_files.sort(key=lambda item: item[0])
    file_manifest.parent.mkdir(parents=True, exist_ok=True)
    file_manifest.write_text(
        "".join(f"{sha256_file(path)}  {relative}\n" for relative, path in checksum_files),
        encoding="utf-8",
        newline="\n",
    )
    print(
        f"python_overlay_manifests files={len(checksum_files)} paths={len(paths)} "
        f"file_manifest_sha256={sha256_file(file_manifest)} "
        f"path_inventory_sha256={sha256_file(path_inventory)}"
    )


def cmd_verify_stage(args: argparse.Namespace, lock: dict, lock_path: Path) -> None:
    payload = verify_stage(Path(args.site), lock, lock_path)
    if args.print_entries:
        for item in payload["managed_entries"]:
            print(item["name"])
    else:
        print(
            f"python_stage_verified entries={len(payload['managed_entries'])} "
            f"lock_sha256={payload['lock_sha256']}"
        )


def run_git(*arguments: str, cwd: Path | None = None) -> str:
    process = subprocess.run(
        ["git", *arguments],
        cwd=cwd,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if process.returncode != 0:
        raise ContractError(f"git {' '.join(arguments)} failed: {process.stderr.strip()}")
    return process.stdout.strip()


def cmd_checkout_harness(args: argparse.Namespace, lock: dict, lock_path: Path) -> None:
    destination = Path(args.destination).resolve()
    source = lock["source"]["harness"]
    if destination.exists():
        if not (destination / ".git").is_dir():
            raise ContractError(f"refusing non-Git harness directory: {destination}")
        dirty = run_git("status", "--porcelain", cwd=destination)
        if dirty:
            raise ContractError(f"refusing dirty Python harness checkout: {destination}")
    else:
        destination.parent.mkdir(parents=True, exist_ok=True)
        run_git("init", str(destination))
        run_git("remote", "add", "origin", source["url"], cwd=destination)
        run_git("fetch", "--depth", "1", "origin", source["commit"], cwd=destination)
        run_git("checkout", "--detach", "FETCH_HEAD", cwd=destination)
    head = run_git("rev-parse", "HEAD", cwd=destination)
    if head != source["commit"]:
        raise ContractError(
            f"Python harness checkout is not pinned commit {source['commit']}: got {head}; "
            "use a new destination instead of mutating it"
        )
    remote = run_git("remote", "get-url", "origin", cwd=destination)
    if remote.rstrip("/") not in {
        source["url"].rstrip("/"),
        "git@github.com:Jiusi-pys/python.git",
    }:
        raise ContractError(f"unexpected Python harness origin: {remote}")
    provenance = {
        "schema_version": 1,
        "generated_at_utc": utc_now(),
        "lock_sha256": lock_sha256(lock_path),
        "url": source["url"],
        "branch_reference": source["branch"],
        "commit": head,
    }
    atomic_json(destination.parent / "python-harness.provenance.json", provenance)
    print(f"python_harness_commit={head}")


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lock", default=str(DEFAULT_LOCK))
    subparsers = parser.add_subparsers(dest="command", required=True)

    verify_runtime_parser = subparsers.add_parser("verify-runtime")
    verify_runtime_parser.add_argument("--root", required=True)
    verify_runtime_parser.add_argument("--write-provenance", action="store_true")
    verify_runtime_parser.add_argument("--origin", default="unspecified")
    verify_runtime_parser.set_defaults(handler=cmd_verify_runtime)

    fetch_parser = subparsers.add_parser("fetch-artifacts")
    fetch_parser.add_argument("--wheels-dir", default=str(WORKSPACE / "python_target" / "wheels"))
    fetch_parser.add_argument("--sources-dir", default=str(WORKSPACE / "python_target" / "sources"))
    fetch_parser.add_argument("--clean", action="store_true")
    fetch_parser.set_defaults(handler=cmd_fetch_artifacts)

    unpack_parser = subparsers.add_parser("unpack-stage")
    unpack_parser.add_argument("--output", required=True)
    unpack_parser.add_argument("--wheels-dir", default=str(WORKSPACE / "python_target" / "wheels"))
    unpack_parser.add_argument("--sources-dir", default=str(WORKSPACE / "python_target" / "sources"))
    unpack_parser.set_defaults(handler=cmd_unpack_stage)

    finalize_parser = subparsers.add_parser("finalize-stage")
    finalize_parser.add_argument("--site", required=True)
    finalize_parser.set_defaults(handler=cmd_finalize_stage)

    verify_stage_parser = subparsers.add_parser("verify-stage")
    verify_stage_parser.add_argument("--site", required=True)
    verify_stage_parser.add_argument("--print-entries", action="store_true")
    verify_stage_parser.set_defaults(handler=cmd_verify_stage)

    deployment_parser = subparsers.add_parser("create-deployment-marker")
    deployment_parser.add_argument("--site", required=True)
    deployment_parser.add_argument("--output", required=True)
    deployment_parser.add_argument("--board", required=True)
    deployment_parser.add_argument("--remote-runtime-prefix", required=True)
    deployment_parser.add_argument("--remote-overlay", required=True)
    deployment_parser.add_argument("--file-manifest", required=True)
    deployment_parser.add_argument("--path-inventory", required=True)
    deployment_parser.add_argument("--runtime-artifact-manifest")
    deployment_parser.add_argument("--require-runtime-artifact", action="store_true")
    deployment_parser.set_defaults(handler=cmd_create_deployment_marker)

    overlay_manifest_parser = subparsers.add_parser("create-overlay-manifests")
    overlay_manifest_parser.add_argument("--site", required=True)
    overlay_manifest_parser.add_argument("--file-manifest", required=True)
    overlay_manifest_parser.add_argument("--path-inventory", required=True)
    overlay_manifest_parser.set_defaults(handler=cmd_create_overlay_manifests)

    harness_parser = subparsers.add_parser("checkout-harness")
    harness_parser.add_argument(
        "--destination", default=str(WORKSPACE / "python_target" / "python-harness")
    )
    harness_parser.set_defaults(handler=cmd_checkout_harness)
    return parser


def main() -> int:
    parser = make_parser()
    args = parser.parse_args()
    lock_path = Path(args.lock).resolve()
    try:
        lock = load_lock(lock_path)
        args.handler(args, lock, lock_path)
    except (ContractError, OSError, subprocess.SubprocessError) as exc:
        print(f"python target contract failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
