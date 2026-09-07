#!/usr/bin/env python3
"""Finish a Windows-produced install prefix for execution on KaihongOS."""
from pathlib import Path
import argparse
import os
import shutil


def finalize(root: Path) -> tuple[int, int]:
    root = root.resolve(strict=True)
    if root.is_symlink() or not root.is_dir():
        raise RuntimeError("install prefix must be a directory")
    # NTFS case-insensitivity can retain the dependency builder's lower-case
    # spelling; the board filesystem requires the exact Lib spelling used by
    # colcon's Windows Python installs and the generic environment.
    libraries = [path for path in root.iterdir() if path.name.lower() == "lib"]
    if len(libraries) != 1 or libraries[0].is_symlink():
        raise RuntimeError("install prefix must have exactly one regular Lib directory")
    if libraries[0].name != "Lib":
        temporary = root / ".ohos-lib-case-normalization"
        if temporary.exists():
            raise RuntimeError("library-name normalization path already exists")
        libraries[0].rename(temporary)
        temporary.rename(root / "Lib")

    dsos = 0
    for source in sorted((root / "opt").glob("*_vendor/lib/lib*.so*")):
        if not source.is_file() or source.is_symlink():
            raise RuntimeError(f"invalid vendor DSO: {source}")
        destination = root / "Lib" / source.name
        if destination.exists() and destination.read_bytes() != source.read_bytes():
            raise RuntimeError(f"conflicting vendor DSO: {destination}")
        shutil.copyfile(source, destination)
        dsos += 1

    wrappers = 0
    scripts = list((root / "Lib").glob("*/*-script.py"))
    scripts.extend((root / "Scripts").glob("*-script.py"))
    for source in sorted(scripts):
        if source.is_symlink():
            raise RuntimeError(f"linked Python launcher: {source}")
        lines = source.read_text(encoding="utf-8").splitlines()
        if not lines or not lines[0].startswith("#!"):
            raise RuntimeError(f"unrecognized Python launcher: {source}")
        # KaihongOS supplies toybox env at /bin/env; /usr/bin/env is absent.
        body = "\n".join(lines[1:]) + "\n"
        payload = ("#!/bin/env python3.12\n" + body).encode()
        legacy_payload = ("#!/usr/bin/env python3.12\n" + body).encode()
        target = source.with_name(source.name.removesuffix("-script.py"))
        if target.exists() and target.read_bytes() not in (payload, legacy_payload):
            raise RuntimeError(f"native launcher would be overwritten: {target}")
        target.write_bytes(payload)
        source.write_bytes(payload)
        target.chmod(0o755)
        launcher = target.with_name(target.name + ".exe")
        if launcher.exists():
            if launcher.is_symlink() or launcher.read_bytes()[:2] != b"MZ":
                raise RuntimeError(f"refusing to remove non-PE launcher: {launcher}")
            launcher.unlink()
        wrappers += 1
    pykdl = root / "Lib/python3.12/dist-packages/PyKDL.so"
    if pykdl.is_file():
        shutil.copyfile(pykdl, root / "Lib/site-packages/PyKDL.cpython-312-aarch64-linux-ohos.so")
    return wrappers, dsos


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("install_root", type=Path)
    args = parser.parse_args()
    wrappers, dsos = finalize(args.install_root)
    print(f"OHOS_INSTALL_FINALIZED python_launchers={wrappers} vendor_dsos={dsos}")
