import hashlib
from pathlib import Path
import tempfile
import unittest
import zipfile
from doctor_runtime import extract_wheel


class DoctorRuntime(unittest.TestCase):
    def run_wheel(self,name,wrong_hash=False):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);wheel=root/'package.whl';site=root/'site';site.mkdir()
            with zipfile.ZipFile(wheel,'w') as archive:archive.writestr(name,b'package')
            sha='0'*64 if wrong_hash else hashlib.sha256(wheel.read_bytes()).hexdigest()
            extract_wheel(wheel,site,sha)
            self.assertEqual((site/name).read_bytes(),b'package')
    def test_regular_module(self):self.run_wheel('rosdistro/__init__.py')
    def test_path_escape(self):
        with self.assertRaises(ValueError):self.run_wheel('../escape.py')
    def test_wrong_digest(self):
        with self.assertRaises(ValueError):self.run_wheel('rosdistro/__init__.py',True)


if __name__=='__main__':unittest.main()
