"""TEST-F001-03: combined M/R/O clean-reproduction acceptance contract."""

from __future__ import annotations

import hashlib
import json
import subprocess
import sys
from pathlib import Path


ROS2_REPO = Path(__file__).resolve().parents[1]
WORKSPACE = ROS2_REPO.parent
APPROVAL = WORKSPACE / "docs" / "agent" / "approval"
BUNDLE = APPROVAL / "v3-bundle"
PLANS = WORKSPACE / "docs" / "agent" / "plans"
MDDS_VALIDATOR = (
    ROS2_REPO / "src" / "Jiusi-pys" / "mdds" / "tools" / "validate_approval_bundle.py"
)
RMW_VALIDATOR = (
    ROS2_REPO
    / "src"
    / "ros2"
    / "rmw_mdds"
    / "tools"
    / "validate_approval_traceability.py"
)
WORKSPACE_VALIDATOR = ROS2_REPO / "scripts" / "validate_f001_approval.py"

EXPECTED_BUNDLE_SHA256 = "D63735E9CDFB485D789358B34081F0193897BE378E35402E8921114703E30A82"
EXPECTED_LOCK_SHA256 = "B897513838336815BBC6F7F1379B400A791099064481091E688BC1163E342360"
EXPECTED_ISO_SHA256 = "DFAA104F0A9BBFEF4FEE4917C49DA5BC778A9422B4F6945224B9CB882E02652D"
EXPECTED_FASTDDS_SHA256 = "2A3C5260E813CB6EB91CE6B75BEB64F364DF004646255913A469483AE1DE8F91"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def tree_fingerprint(root: Path) -> tuple[tuple[str, int, str], ...]:
    return tuple(
        (path.relative_to(root).as_posix(), path.stat().st_size, sha256(path))
        for path in sorted(root.rglob("*"))
        if path.is_file()
    )


def test_f001_03_two_clean_reproductions_and_combined_validation(tmp_path: Path) -> None:
    """TEST-F001-03 reproduces both manifests twice and validates M/R/O state."""
    for validator in (MDDS_VALIDATOR, RMW_VALIDATOR, WORKSPACE_VALIDATOR):
        assert validator.is_file(), f"missing production validator: {validator}"
    reproduction_root = tmp_path / "reproductions"
    approval_before = tree_fingerprint(APPROVAL)
    result = subprocess.run(
        [
            sys.executable,
            str(WORKSPACE_VALIDATOR),
            "--workspace",
            str(WORKSPACE),
            "--bundle",
            str(BUNDLE),
            "--plans",
            str(PLANS),
            "--mdds-validator",
            str(MDDS_VALIDATOR),
            "--rmw-validator",
            str(RMW_VALIDATOR),
            "--expected-bundle-sha256",
            EXPECTED_BUNDLE_SHA256,
            "--expected-lock-sha256",
            EXPECTED_LOCK_SHA256,
            "--reproduction-root",
            str(reproduction_root),
            "--reproduce-count",
            "2",
            "--json",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=600,
        check=False,
    )

    assert result.returncode == 0, result.stdout
    lines = [line for line in result.stdout.splitlines() if line.strip()]
    assert lines, "workspace validator emitted no result"
    payload = json.loads(lines[-1])
    assert payload["schema"] == "ros2.f001-workspace-validation/v1"
    assert payload["result"] == "PASS"
    assert payload["mode"] == "READ_ONLY"
    assert payload["bundle_sha256"] == EXPECTED_BUNDLE_SHA256
    assert payload["lock_sha256"] == EXPECTED_LOCK_SHA256
    assert payload["dag"] == {"nodes": 106, "edges": 521}
    assert payload["traceability"] == {
        "mapping_rows": 3159,
        "mapped_features": 15,
        "mapped_tests": 39,
    }
    reproductions = payload["reproductions"]
    assert len(reproductions) == 2
    assert all(row["iso_sha256"] == EXPECTED_ISO_SHA256 for row in reproductions)
    assert all(row["fastdds_sha256"] == EXPECTED_FASTDDS_SHA256 for row in reproductions)
    assert all(row["equal_to_canonical"] is True for row in reproductions)
    assert len({row["directory"] for row in reproductions}) == 2
    for row in reproductions:
        directory = Path(row["directory"]).resolve()
        assert directory.is_relative_to(reproduction_root.resolve())
        assert (directory / "iso-cxx-psm-1.0.json").is_file()
        assert (directory / "fastdds-2.14.6-dcps-clang.json").is_file()
    assert approval_before == tree_fingerprint(APPROVAL), (
        "combined validation modified the Main-owned approval oracle"
    )
