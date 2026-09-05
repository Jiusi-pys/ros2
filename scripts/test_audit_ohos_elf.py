import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from audit_ohos_elf import audit


class ElfAuditTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.header = b"\x7fELF\x02\x01" + b"\0" * 12 + b"\xb7\x00"
        (self.root / "libsample.so").write_bytes(self.header)

    def output(self, dynamic):
        return patch("audit_ohos_elf.subprocess.run", return_value=subprocess.CompletedProcess([], 0, dynamic, ""))

    def test_origin_is_allowed(self):
        with self.output("  (RUNPATH) Library runpath: [$ORIGIN/../lib]"):
            self.assertEqual(audit(self.root, Path("readelf")), 1)

    def test_host_or_corrupted_origin_is_rejected(self):
        for runtime_path in ("C:/work/install/lib", "/var/tmp/build/lib", "RIGIN/../lib"):
            with self.subTest(runtime_path=runtime_path), self.output(f"(RUNPATH) [{runtime_path}]"):
                with self.assertRaisesRegex(RuntimeError, "build-host runtime"):
                    audit(self.root, Path("readelf"))

    def test_wrong_architecture_is_rejected(self):
        (self.root / "libsample.so").write_bytes(self.header[:-2] + b"\x3e\0")
        with self.assertRaisesRegex(RuntimeError, "non-AArch64"):
            audit(self.root, Path("readelf"))


if __name__ == "__main__":
    unittest.main()
