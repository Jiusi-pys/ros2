import tempfile
import unittest
from pathlib import Path

from finalize_ohos_install import finalize


class InstallFinalizationTests(unittest.TestCase):
    def test_windows_entrypoint_becomes_portable_canonical_launcher(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / "lib" / "demo_nodes_py"
            package.mkdir(parents=True)
            (package / "talker-script.py").write_bytes(b"#!C:\\host\\python.exe\r\nprint('run')\r\n")
            (package / "talker.exe").write_bytes(b"MZhost stub")
            (package / "user-data.txt").write_text("keep")
            self.assertEqual(finalize(root), (1, 0))
            package = root / "Lib" / "demo_nodes_py"
            self.assertEqual((package / "talker").read_bytes(), b"#!/usr/bin/env python3.12\nprint('run')\n")
            self.assertFalse((package / "talker.exe").exists())
            self.assertEqual((package / "user-data.txt").read_text(), "keep")
            self.assertEqual(finalize(root), (1, 0))

    def test_native_executable_collision_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / "Lib" / "demo"
            package.mkdir(parents=True)
            (package / "node-script.py").write_text("#!/host/python\nprint('x')\n")
            (package / "node").write_bytes(b"\x7fELFkeep")
            with self.assertRaisesRegex(RuntimeError, "native launcher would be overwritten"):
                finalize(root)
            self.assertEqual((package / "node").read_bytes(), b"\x7fELFkeep")


if __name__ == "__main__":
    unittest.main()
