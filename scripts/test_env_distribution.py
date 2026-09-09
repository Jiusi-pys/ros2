"""Source the guarded deployment template and verify its distribution identity."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
BASH='C:/Program Files/Git/bin/bash.exe'


class DistributionEnvironment(unittest.TestCase):
    def source(self,inherited):
        with tempfile.TemporaryDirectory(prefix='ros2_distro_env_') as folder:
            prefix=Path(folder);(prefix/'bin').mkdir();(prefix/'.ros2_deploy_complete').write_text('ready\n')
            script='''
grep() { return 0; }
export ROS2_HOME="$1" ROS2_DEPLOY_EXPECTED_MARKER=ready
. "$2" || exit $?
printf '%s:%s:%s\\n' "${ROS_DISTRO:-}" "${ROS_VERSION:-}" "${ROS_PYTHON_VERSION:-}"
'''
            env=dict(os.environ)
            for key in ('ROS_DISTRO','ROS_VERSION','ROS_PYTHON_VERSION'):env.pop(key,None)
            env.update(inherited)
            result=subprocess.run([BASH,'-c',script,'sh',prefix.as_posix(),(ROOT/'scripts/env_ohos.template.sh').as_posix()],env=env,capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,0,result.stderr)
            return result.stdout.strip()
    def test_exports_jazzy_identity(self):self.assertEqual(self.source({}),'jazzy:2:3')
    def test_does_not_inherit_other_distribution(self):self.assertEqual(self.source({'ROS_DISTRO':'rolling','ROS_VERSION':'1','ROS_PYTHON_VERSION':'2'}),'jazzy:2:3')


if __name__=='__main__':unittest.main()
