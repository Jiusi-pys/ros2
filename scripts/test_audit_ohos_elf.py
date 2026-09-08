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

    def test_host_python_suffix_is_rejected_even_for_aarch64_elf(self):
        for name in ('_rclpy.cp312-win_amd64.pyd',
                     '_module.cpython-312-x86_64-linux-gnu.so'):
            with self.subTest(name=name), self.output(''):
                path = self.root / name
                path.write_bytes(self.header)
                try:
                    with self.assertRaisesRegex(RuntimeError, 'Python extension suffix'):
                        audit(self.root, Path('readelf'))
                finally:
                    path.unlink()

    def test_unavailable_launcher_interpreter_is_rejected(self):
        package = self.root / 'Lib/demo_nodes_py'
        package.mkdir(parents=True)
        payload = b'#!/usr/bin/env python3.12\nprint(1)\n'
        (package / 'talker-script.py').write_bytes(payload)
        (package / 'talker').write_bytes(payload)
        with self.output(''):
            with self.assertRaisesRegex(RuntimeError, 'launcher interpreter'):
                audit(self.root, Path('readelf'))

    def test_available_launcher_interpreter_is_accepted(self):
        package = self.root / 'Lib/demo_nodes_py'
        package.mkdir(parents=True)
        payload = b'#!/bin/env python3.12\nprint(1)\n'
        (package / 'talker-script.py').write_bytes(payload)
        (package / 'talker').write_bytes(payload)
        with self.output(''):
            self.assertEqual(audit(self.root, Path('readelf')), 1)


if __name__ == "__main__":
    unittest.main()
