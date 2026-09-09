#!/usr/bin/env python3
"""Resolve the existing source directories explicitly selected by a vcs manifest."""

import argparse
from pathlib import Path, PurePosixPath, PureWindowsPath
import sys

import yaml


def source_roots(manifest: Path, source_root: Path) -> list[Path]:
    data = yaml.safe_load(manifest.read_text(encoding="utf-8"))
    repositories = data.get("repositories") if isinstance(data, dict) else None
    if not isinstance(repositories, dict) or not repositories:
        raise ValueError("manifest requires a nonempty repositories mapping")
    root = source_root.resolve(strict=True)
    if not root.is_dir():
        raise ValueError("source root must be a directory")
    roots = set()
    missing = []
    for name, spec in repositories.items():
        if not isinstance(name, str) or not name or "\\" in name or "\n" in name or "\r" in name:
            raise ValueError(f"invalid repository path: {name!r}")
        path = PurePosixPath(name)
        if (path.is_absolute() or PureWindowsPath(name).drive or ".." in path.parts
                or not path.parts or path == PurePosixPath(".")):
            raise ValueError(f"unsafe repository path: {name!r}")
        if (not isinstance(spec, dict) or set(spec) != {"type", "url", "version"}
                or spec["type"] != "git"
                or any(not isinstance(spec[k], str) or not spec[k].strip() for k in spec)):
            raise ValueError(f"invalid repository specification: {name!r}")
        candidate = (root / path).resolve()
        if candidate == root or not candidate.is_relative_to(root):
            raise ValueError(f"repository escapes source root: {name!r}")
        if not candidate.exists():
            missing.append(name)
        elif not candidate.is_dir():
            raise ValueError(f"repository is not a directory: {name!r}")
        else:
            roots.add(candidate)
    if not roots:
        raise ValueError("manifest has no existing source directories")
    for name in sorted(missing):
        print(f"warning: source directory missing: {name}", file=sys.stderr)
    return sorted(roots, key=str)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--source-root", required=True, type=Path)
    args = parser.parse_args()
    try:
        roots = source_roots(args.manifest, args.source_root)
    except (OSError, RuntimeError, ValueError, yaml.YAMLError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    print("\n".join(map(str, roots)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
