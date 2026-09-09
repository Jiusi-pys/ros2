#!/usr/bin/env python3
"""Compose F001 validation and reproduce both frozen API manifests."""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import platform
import subprocess
import sys
import zipfile
from pathlib import Path
from typing import Any


SCHEMA = "ros2.f001-workspace-validation/v1"
EXPECTED_ISO_SHA256 = "DFAA104F0A9BBFEF4FEE4917C49DA5BC778A9422B4F6945224B9CB882E02652D"
EXPECTED_FASTDDS_SHA256 = "2A3C5260E813CB6EB91CE6B75BEB64F364DF004646255913A469483AE1DE8F91"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def run_process(command: list[str], label: str, timeout: int) -> str:
    result = subprocess.run(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=timeout,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(f"{label} failed: {result.stdout.strip()[-6000:]}")
    return result.stdout


def last_pass_json(output: str, label: str) -> dict[str, Any]:
    lines = [line for line in output.splitlines() if line.strip()]
    if not lines:
        raise RuntimeError(f"{label} emitted no result")
    value = json.loads(lines[-1])
    if not isinstance(value, dict) or value.get("result") != "PASS":
        raise RuntimeError(f"{label} did not return PASS")
    return value


def run_json_validator(command: list[str], label: str) -> dict[str, Any]:
    return last_pass_json(run_process(command, label, 180), label)


def extract_tool_wheels(tool_cache: Path, tool_site: Path) -> None:
    wheels = [
        tool_cache / "tree_sitter-0.25.2-cp312-cp312-win_amd64.whl",
        tool_cache / "tree_sitter_cpp-0.23.4-cp39-abi3-win_amd64.whl",
        tool_cache / "clang-15.0.7-py3-none-any.whl",
    ]
    tool_site.mkdir()
    for wheel in wheels:
        if not wheel.is_file():
            raise RuntimeError(f"missing frozen extraction tool: {wheel}")
        with zipfile.ZipFile(wheel) as archive:
            archive.extractall(tool_site)


def reproduce_manifest_pair(workspace: Path, run_directory: Path) -> dict[str, Any]:
    run_directory.mkdir(parents=True, exist_ok=False)
    approval = workspace / "docs" / "agent" / "approval"
    inputs = workspace / "out" / "agent-approval-inputs"
    sdk = Path(r"C:\Users\17715\AppData\Local\OpenHarmony\Sdk\23\native")
    tool_site = run_directory / "tool-site"
    extract_tool_wheels(inputs / "tools", tool_site)

    run_process(
        [
            sys.executable,
            str(approval / "generate_api_manifests.py"),
            "--tool-site",
            str(tool_site),
            "--iso-root",
            str(inputs / "omg" / "psm-sources" / "hpp"),
            "--fastdds-root",
            str(inputs / "fastdds-f2ad29f"),
            "--output",
            str(run_directory),
            "--only",
            "iso",
        ],
        "ISO C++ PSM manifest reproduction",
        180,
    )
    run_process(
        [
            sys.executable,
            str(approval / "generate_fastdds_clang_manifest.py"),
            "--tool-site",
            str(tool_site),
            "--libclang",
            str(sdk / "llvm" / "bin" / "libclang.dll"),
            "--source-root",
            str(inputs / "fastdds-f2ad29f"),
            "--config-include",
            str(inputs / "fastdds-config"),
            "--fastcdr-include",
            str(inputs / "fastcdr-29b66" / "include"),
            "--boost-include",
            str(inputs / "boost-f2ad29f" / "thirdparty" / "boost" / "include"),
            "--sysroot",
            str(sdk / "sysroot"),
            "--libcxx-include",
            str(sdk / "llvm" / "include" / "libcxx-ohos" / "include" / "c++" / "v1"),
            "--output",
            str(run_directory / "fastdds-2.14.6-dcps-clang.json"),
        ],
        "Fast DDS clang manifest reproduction",
        540,
    )

    iso_path = run_directory / "iso-cxx-psm-1.0.json"
    fast_path = run_directory / "fastdds-2.14.6-dcps-clang.json"
    iso_digest = sha256(iso_path)
    fast_digest = sha256(fast_path)
    if iso_digest != EXPECTED_ISO_SHA256:
        raise RuntimeError(
            f"ISO reproduction mismatch: expected {EXPECTED_ISO_SHA256}, observed {iso_digest}"
        )
    if fast_digest != EXPECTED_FASTDDS_SHA256:
        raise RuntimeError(
            "Fast DDS reproduction mismatch: "
            f"expected {EXPECTED_FASTDDS_SHA256}, observed {fast_digest}"
        )
    return {
        "directory": str(run_directory.resolve()),
        "iso_sha256": iso_digest,
        "fastdds_sha256": fast_digest,
        "equal_to_canonical": True,
    }


def validate(args: argparse.Namespace) -> dict[str, Any]:
    workspace = args.workspace.resolve()
    bundle = args.bundle.resolve()
    plans = args.plans.resolve()
    mdds_validator = args.mdds_validator.resolve()
    rmw_validator = args.rmw_validator.resolve()
    reproduction_root = args.reproduction_root.resolve()
    if args.reproduce_count != 2:
        raise RuntimeError("the approved F001 contract requires exactly two clean reproductions")
    for path, label in (
        (mdds_validator, "M validator"),
        (rmw_validator, "R validator"),
    ):
        if not path.is_file():
            raise RuntimeError(f"missing {label}: {path}")

    mdds = run_json_validator(
        [
            sys.executable,
            str(mdds_validator),
            "--workspace",
            str(workspace),
            "--bundle",
            str(bundle),
            "--expected-bundle-sha256",
            args.expected_bundle_sha256,
            "--expected-lock-sha256",
            args.expected_lock_sha256,
            "--json",
        ],
        "M approval-bundle validator",
    )
    rmw = run_json_validator(
        [
            sys.executable,
            str(rmw_validator),
            "--workspace",
            str(workspace),
            "--bundle",
            str(bundle),
            "--plans",
            str(plans),
            "--expected-bundle-sha256",
            args.expected_bundle_sha256,
            "--expected-lock-sha256",
            args.expected_lock_sha256,
            "--json",
        ],
        "R approval-traceability validator",
    )

    reproduction_root.mkdir(parents=True, exist_ok=True)
    run_directories = [
        reproduction_root / f"run-{index + 1:02d}" for index in range(args.reproduce_count)
    ]
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.reproduce_count) as executor:
        futures = [
            executor.submit(reproduce_manifest_pair, workspace, run_directory)
            for run_directory in run_directories
        ]
        reproductions = [future.result() for future in futures]
    canonical_iso = bundle / "iso-cxx-psm-1.0.json"
    canonical_fast = bundle / "fastdds-2.14.6-dcps-clang.json"
    if sha256(canonical_iso) != EXPECTED_ISO_SHA256 or sha256(canonical_fast) != EXPECTED_FASTDDS_SHA256:
        raise RuntimeError("canonical manifest identity changed after reproduction")

    counts = rmw["counts"]
    approval = workspace / "docs" / "agent" / "approval"
    fingerprints = {
        "python": platform.python_version(),
        "platform": platform.platform(),
        "mdds_validator_sha256": sha256(mdds_validator),
        "rmw_validator_sha256": sha256(rmw_validator),
        "iso_generator_sha256": sha256(approval / "generate_api_manifests.py"),
        "fastdds_generator_sha256": sha256(approval / "generate_fastdds_clang_manifest.py"),
        "approval_inputs_sha256": sha256(bundle / "approval-inputs.json"),
        "reproduce_count": args.reproduce_count,
    }
    return {
        "schema": SCHEMA,
        "result": "PASS",
        "mode": "READ_ONLY",
        "bundle_sha256": mdds["bundle_sha256"],
        "lock_sha256": mdds["lock_sha256"],
        "dag": {"nodes": counts["dag_nodes"], "edges": counts["dag_edges"]},
        "traceability": {
            "mapping_rows": counts["mapping_rows"],
            "mapped_features": counts["mapped_features"],
            "mapped_tests": counts["mapped_tests"],
        },
        "reproductions": reproductions,
        "fingerprints": fingerprints,
    }


def emit(payload: dict[str, Any], as_json: bool) -> None:
    if as_json:
        print(json.dumps(payload, ensure_ascii=False, sort_keys=True, separators=(",", ":")))
    elif payload["result"] == "PASS":
        print(
            f"F001_WORKSPACE_VALID bundle_sha256={payload['bundle_sha256']} "
            f"reproductions={len(payload['reproductions'])}"
        )
    else:
        print(f"F001_WORKSPACE_INVALID error={payload['error']}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--workspace", type=Path, required=True)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--plans", type=Path, required=True)
    parser.add_argument("--mdds-validator", type=Path, required=True)
    parser.add_argument("--rmw-validator", type=Path, required=True)
    parser.add_argument("--expected-bundle-sha256", required=True)
    parser.add_argument("--expected-lock-sha256", required=True)
    parser.add_argument("--reproduction-root", type=Path, required=True)
    parser.add_argument("--reproduce-count", type=int, default=2)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    try:
        payload = validate(args)
    except Exception as exc:  # Fail closed at the CLI boundary.
        payload = {"schema": SCHEMA, "result": "FAIL", "mode": "READ_ONLY", "error": str(exc)}
        emit(payload, args.json)
        return 1
    emit(payload, args.json)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
