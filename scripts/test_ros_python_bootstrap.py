import importlib.util
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch


class BootstrapTests(unittest.TestCase):
    def test_profile_scoping_and_global_visibility(self):
        source = Path(__file__).with_name('runtime_config') / 'sitecustomize.py'
        self.assertTrue(source.is_file())
        spec = importlib.util.spec_from_file_location('ros_bootstrap_test', source)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        directory = str(source.parent.resolve())
        cases = [
            ('cpython-312-aarch64-linux-ohos', directory, '/data/local/tmp/ros2-generic', True),
            ('cp312-win_amd64', directory, '/data/local/tmp/ros2-generic', False),
            ('cpython-312-aarch64-linux-ohos', '', '/data/local/tmp/ros2-generic', False),
            ('cpython-312-aarch64-linux-ohos', directory, '', False),
        ]
        for abi, configured, ros_home, expected in cases:
            with self.subTest(abi=abi, configured=configured, ros_home=ros_home), \
                    patch.dict(os.environ, {'ROS2_PYTHON_BOOTSTRAP_DIR': configured,
                                            'ROS2_HOME': ros_home}, clear=True), \
                    patch.object(module.sysconfig, 'get_config_var', return_value=abi), \
                    patch.object(sys, 'getdlopenflags', return_value=2, create=True), \
                    patch.object(sys, 'setdlopenflags', create=True) as setter, \
                    patch.object(os, 'RTLD_GLOBAL', 256, create=True):
                module.configure_ros_python()
                if expected:
                    setter.assert_called_once_with(258)
                else:
                    setter.assert_not_called()


if __name__ == '__main__':
    unittest.main()
