#!/usr/bin/env python3
"""Remove ephemeral build-root paths from an installed cross CPython tree."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess


CANONICAL_ROOT = "/opt/ros2-ohos-python-source"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", required=True)
    parser.add_argument("--source", required=True)
    parser.add_argument("--build", required=True)
    parser.add_argument("--host-python", required=True)
    parser.add_argument("--toolchain", required=True)
    parser.add_argument("--sysroot", required=True)
    parser.add_argument("--deps-prefix", required=True)
    parser.add_argument("--destdir", required=True)
    parser.add_argument("--llvm-strip", required=True)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    runtime = Path(args.runtime).resolve()
    llvm_strip = Path(args.llvm_strip).resolve()
    if not llvm_strip.is_file():
        raise SystemExit(f"llvm-strip is missing: {llvm_strip}")
    replacements = {
        str(Path(args.host_python).resolve()): f"{CANONICAL_ROOT}/host/bin/python3.12",
        str(Path(args.source).resolve()): f"{CANONICAL_ROOT}/cpython",
        str(Path(args.build).resolve().parent): f"{CANONICAL_ROOT}/output",
        str(Path(args.build).resolve()): f"{CANONICAL_ROOT}/target-build",
        str(Path(args.toolchain).resolve()): f"{CANONICAL_ROOT}/clang",
        str(Path(args.sysroot).resolve()): f"{CANONICAL_ROOT}/ohos-native/sysroot",
        str(Path(args.deps_prefix).resolve()): f"{CANONICAL_ROOT}/deps",
        str(Path(args.destdir).resolve()): f"{CANONICAL_ROOT}/destdir",
    }
    if len(replacements) != 8:
        raise SystemExit("normalization inputs unexpectedly alias each other")

    removed_build_only: list[str] = []
    config_dirs = list((runtime / "lib" / "python3.12").glob("config-3.12-*"))
    if len(config_dirs) != 1:
        raise SystemExit("expected exactly one installed CPython config directory")
    for name in ("libpython3.12.a", "python.o", "config.c", "python-config.py"):
        path = config_dirs[0] / name
        if path.is_file() and not path.is_symlink():
            removed_build_only.append(path.relative_to(runtime).as_posix())
            path.unlink()

    removed_pyc: list[str] = []
    for path in sorted(runtime.rglob("*.pyc")):
        if path.is_file() and not path.is_symlink():
            removed_pyc.append(path.relative_to(runtime).as_posix())
            path.unlink()
    for path in sorted(runtime.rglob("__pycache__"), reverse=True):
        if path.is_dir() and not any(path.iterdir()):
            path.rmdir()

    stripped: list[str] = []
    for path in sorted(runtime.rglob("*")):
        if path.is_symlink() or not path.is_file():
            continue
        with path.open("rb") as stream:
            is_elf = stream.read(4) == b"\x7fELF"
        if is_elf:
            subprocess.run([str(llvm_strip), "--strip-debug", str(path)], check=True)
            stripped.append(path.relative_to(runtime).as_posix())

    changed: list[str] = []
    for path in sorted(runtime.rglob("*")):
        if not path.is_file() or path.is_symlink():
            continue
        data = path.read_bytes()
        if b"\0" in data:
            continue
        try:
            text = data.decode("utf-8")
        except UnicodeDecodeError:
            continue
        original = text
        for source, target in sorted(replacements.items(), key=lambda item: -len(item[0])):
            text = text.replace(source, target)
        if text != original:
            with path.open("w", encoding="utf-8", newline="\n") as stream:
                stream.write(text)
            changed.append(path.relative_to(runtime).as_posix())

    for path in sorted(runtime.rglob("*")):
        if not path.is_file() or path.is_symlink():
            continue
        data = path.read_bytes()
        if b"\0" in data:
            continue
        for source in replacements:
            if source.encode() in data:
                raise SystemExit(f"ephemeral build path remains in {path}: {source}")

    print(f"removed_pyc_files={len(removed_pyc)}")
    print(f"removed_build_only_files={len(removed_build_only)}")
    print(f"stripped_elf_files={len(stripped)}")
    print(f"normalized_files={len(changed)}")
    for name in changed:
        print(f"normalized={name}")


if __name__ == "__main__":
    main()
