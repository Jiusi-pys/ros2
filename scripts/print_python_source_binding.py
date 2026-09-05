#!/usr/bin/env python3
"""Print a fixed five-line, verified source-receipt binding for shell clients."""
import argparse
from pathlib import Path
import re

import python_runtime_artifact as artifact


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--lock", type=Path, required=True)
    parser.add_argument("--deployment-marker", type=Path)
    args = parser.parse_args()
    import json
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    mode = manifest.get("provenance_mode")
    marker = None
    if args.deployment_marker:
        marker = json.loads(args.deployment_marker.read_text(encoding="utf-8"))
        if marker.get("complete") is not True or marker.get("runtime_provenance_mode") != mode:
            raise ValueError("deployment marker mode/complete differs from artifact")
    if mode == "artifact-reproducible-not-source-reproducible":
        if "source_build_receipt" in manifest:
            raise ValueError("artifact-only manifest contains source receipt")
        if marker and any(marker.get(key) not in (None, "NOT_APPLICABLE") for key in (
            "python_source_build_receipt_sha256", "python_source_lock_sha256",
            "python_source_build_recipe_sha256",
        )):
            raise ValueError("artifact-only marker contains source binding")
        print("\n".join([mode] + ["NOT_APPLICABLE"] * 4))
        return
    if mode != "source-reproducible":
        raise ValueError("unknown Python provenance mode")
    verified = artifact.verify_source_receipt(args.manifest, manifest, args.lock)
    receipt = verified["receipt"]
    filename = Path(verified["path"]).name
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", filename):
        raise ValueError("unsafe source receipt basename")
    hashes = [verified["sha256"], receipt["python_source_lock_sha256"], receipt["build_recipe_sha256"]]
    if any(not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{64}", value) for value in hashes):
        raise ValueError("invalid source receipt digest")
    if marker:
        for key, value in zip(("python_source_build_receipt_sha256", "python_source_lock_sha256",
                               "python_source_build_recipe_sha256"), hashes):
            if marker.get(key) != value:
                raise ValueError(f"deployment marker source binding mismatch: {key}")
    print("\n".join([mode, filename, *hashes]))


if __name__ == "__main__":
    main()
