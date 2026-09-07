"""Verify doctor dependencies are locked and importable from the target stage."""
import json
from pathlib import Path
import subprocess
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
PROJECTS=('rosdistro','rospkg','distro')


class DoctorDependencies(unittest.TestCase):
    def test_dependency_closure_is_locked(self):
        lock=json.loads((ROOT/'scripts/python/ohos_python.lock.json').read_text())
        names={p['project'].lower().replace('_','-') for p in lock['wheels']}
        self.assertTrue(set(PROJECTS)<=names)

    def test_actual_staged_modules_match_locked_versions(self):
        code='''
import importlib,importlib.metadata,json,sys
from pathlib import Path
site=Path(sys.argv[1]).resolve();sys.path.insert(0,str(site))
lock=json.loads(Path(sys.argv[2]).read_text());versions={p['project'].lower():p['version'] for p in lock['wheels']}
for name in ('rosdistro','rospkg','distro'):
 module=importlib.import_module(name)
 assert Path(module.__file__).resolve().is_relative_to(site)
 assert importlib.metadata.version(name)==versions[name]
import rosdistro,rospkg,distro
assert callable(rosdistro.get_distribution) and callable(rospkg.RosPack) and callable(distro.id)
print('DOCTOR_DEPENDENCIES_OK')
'''
        result=subprocess.run([sys.executable,'-I','-B','-c',code,str(ROOT/'python_target/sitepkgs'),str(ROOT/'scripts/python/ohos_python.lock.json')],capture_output=True,text=True,timeout=20)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('DOCTOR_DEPENDENCIES_OK',result.stdout)


if __name__=='__main__':unittest.main()
