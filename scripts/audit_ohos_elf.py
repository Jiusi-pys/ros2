#!/usr/bin/env python3
"""Reject wrong-architecture ELF files and host runtime paths before sealing."""
import argparse
from pathlib import Path
import re
import subprocess


HOST_PATH = re.compile(r"[A-Za-z]:[/\\]|/(?:mnt|var/tmp|home|Users)/|(?<!O)RIGIN")


def audit(root: Path, readelf: Path) -> int:
    count = 0
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        with path.open("rb") as stream:
            header = stream.read(20)
        if header[:4] != b"\x7fELF":
            continue
        if len(header) != 20 or header[4:6] != b"\x02\x01" or int.from_bytes(header[18:20], "little") != 183:
            raise RuntimeError(f"non-AArch64/little-endian ELF in target prefix: {path}")
        dynamic = subprocess.run(
            [str(readelf), "--dynamic", str(path)],
            check=True, capture_output=True, text=True,
        ).stdout
        for line in dynamic.splitlines():
            if re.search(r"\((?:RPATH|RUNPATH)\)", line) and HOST_PATH.search(line):
                raise RuntimeError(f"build-host runtime search path in {path}: {line.strip()}")
        count += 1
    if not count:
        raise RuntimeError("target prefix contains no ELF artifacts")
    return count


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--readelf", required=True, type=Path)
    args = parser.parse_args()
    print(f"OHOS_ELF_AUDIT result=PASS files={audit(args.root, args.readelf)}")
