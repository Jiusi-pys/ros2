from __future__ import annotations

import tempfile
import json
import unittest
from pathlib import Path

import ohos_build_receipt as receipt


class BuildReceiptTests(unittest.TestCase):
    def test_begin_receipt_has_no_optional_middleware_build_switch(self) -> None:
        import argparse
        begin = next(action for action in receipt.parser()._actions
                     if isinstance(action, argparse._SubParsersAction)).choices["begin"]
        self.assertNotIn("--build-mdds", begin.format_help())

    def test_clean_release_rejects_artifact_only_and_unknown_python_modes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            manifest = Path(directory) / "runtime.manifest.json"
            for mode in (None, "artifact-reproducible-not-source-reproducible", "UNKNOWN"):
                manifest.write_text(json.dumps({"provenance_mode": mode}), encoding="utf-8")
                missing = Path(directory) / "missing"
                with self.assertRaisesRegex(receipt.ReceiptError, "source-build receipt"):
                    receipt.validate_python_inputs(manifest, missing, missing, missing, missing)

    def test_expected_packages_are_unique_and_sorted(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "packages.txt"
            path.write_text("a_pkg\nz_pkg\n", encoding="utf-8")
            self.assertEqual(["a_pkg", "z_pkg"], receipt.read_expected_packages(path))
            path.write_text("z_pkg\na_pkg\n", encoding="utf-8")
            with self.assertRaises(receipt.ReceiptError):
                receipt.read_expected_packages(path)

    def test_job_terminal_parser_exposes_name_and_return_code(self) -> None:
        log = (
            "[1.000000] (alpha) JobEnded: {'identifier': 'alpha', 'rc': 0}\n"
            "[2.000000] (beta) JobEnded: {'identifier': 'beta', 'rc': 7}\n"
        )
        self.assertEqual(
            [("alpha", "alpha", "0"), ("beta", "beta", "7")],
            receipt.JOB_ENDED.findall(log),
        )

    def test_recorded_relative_paths_stay_under_workspace(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace = Path(directory).resolve()
            path = workspace / "scripts" / "input.txt"
            path.parent.mkdir()
            path.write_text("x", encoding="utf-8")
            label = receipt.path_label(path, workspace)
            self.assertEqual("scripts/input.txt", label)
            self.assertEqual(path, receipt.recorded_path(label, workspace))


if __name__ == "__main__":
    unittest.main()
