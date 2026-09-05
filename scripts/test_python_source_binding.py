import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import print_python_source_binding as binding


class SourceBindingTests(unittest.TestCase):
    def invoke(self, manifest, marker=None, verified=None):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            artifact = root / "artifact.json"
            artifact.write_text(json.dumps(manifest))
            argv = ["binding", "--manifest", str(artifact), "--lock", str(root / "lock.json")]
            if marker is not None:
                marker_path = root / "marker.json"
                marker_path.write_text(json.dumps(marker))
                argv.extend(["--deployment-marker", str(marker_path)])
            output = io.StringIO()
            # This tests only the shell adapter; cryptographic/source validation
            # belongs to the real verifier's separate tests and final artifact.
            with mock.patch("sys.argv", argv), contextlib.redirect_stdout(output), mock.patch.object(
                binding.artifact, "verify_source_receipt", return_value=verified, create=True,
            ) as verifier:
                binding.main()
                return output.getvalue().splitlines(), verifier.call_count

    def test_artifact_only_has_no_source_claim(self):
        mode = "artifact-reproducible-not-source-reproducible"
        lines, calls = self.invoke({"provenance_mode": mode}, {"complete": True, "runtime_provenance_mode": mode})
        self.assertEqual(lines, [mode] + ["NOT_APPLICABLE"] * 4)
        self.assertEqual(calls, 0)

    def test_unknown_or_contradictory_mode_rejected(self):
        for manifest in ({"provenance_mode": "invented"}, {
            "provenance_mode": "artifact-reproducible-not-source-reproducible", "source_build_receipt": {},
        }):
            with self.assertRaises(ValueError):
                self.invoke(manifest)

    def test_source_requires_matching_marker(self):
        verified = {"path": "receipt.json", "sha256": "a" * 64, "receipt": {
            "python_source_lock_sha256": "b" * 64, "build_recipe_sha256": "c" * 64,
        }}
        manifest = {"provenance_mode": "source-reproducible"}
        marker = {"complete": True, "runtime_provenance_mode": "source-reproducible",
                  "python_source_build_receipt_sha256": "a" * 64,
                  "python_source_lock_sha256": "b" * 64,
                  "python_source_build_recipe_sha256": "c" * 64}
        lines, calls = self.invoke(manifest, marker, verified)
        self.assertEqual(lines, ["source-reproducible", "receipt.json", "a" * 64, "b" * 64, "c" * 64])
        self.assertEqual(calls, 1)
        marker["python_source_lock_sha256"] = "d" * 64
        with self.assertRaisesRegex(ValueError, "binding mismatch"):
            self.invoke(manifest, marker, verified)


if __name__ == "__main__":
    unittest.main()
