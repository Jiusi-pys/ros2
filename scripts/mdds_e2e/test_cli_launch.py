import unittest
from pathlib import Path
import tempfile
from unittest.mock import patch
from cli_process import recipe,wait_executable,native_libraries


class LaunchRecipe(unittest.TestCase):
    def test_staged_rclcpp_is_required_in_native_mappings(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/'lib').mkdir()
            self.assertEqual(len(native_libraries(root)),3)
            (root/'lib/librclcpp.so').write_bytes(b'fixed')
            self.assertEqual(len(native_libraries(root)),4)
            self.assertIn(str(root/'lib/librclcpp.so'),native_libraries(root))
    def test_actual_launch_and_declared_remappings(self):
        case,label,argv,expected=recipe('/ros_broker_test','B',mode='launch')[0]
        self.assertEqual(case,'cli:launch');self.assertEqual(label,'process_launch')
        self.assertEqual(argv[:3],['launch','--noninteractive','/data/local/tmp/ros2/.mdds-owned-runs/test/process_talker.launch.py'])
        self.assertIn('node_name:=launch_A',argv);self.assertIn('output_topic:=/process_test/A/out',argv)
        self.assertIn('__node:=launch_A',expected['native_args'])
    def test_waits_through_pre_exec_child(self):
        with patch('cli_process.os.readlink',side_effect=['/usr/bin/python3.12','/owned/talker']) as read,patch('cli_process.process_start',return_value='789'),patch('cli_process.time.sleep'),patch('cli_process.time.monotonic',side_effect=[0,0,.1]):
            wait_executable(124,'/owned/talker','789')
        self.assertEqual(read.call_count,2)
    def test_reused_pid_rejected(self):
        with patch('cli_process.process_start',return_value='other'):
            with self.assertRaises(ValueError):wait_executable(124,'/owned/talker','789')
    def test_never_accepts_wrong_executable(self):
        with patch('cli_process.os.readlink',return_value='/wrong'),patch('cli_process.process_start',return_value='789'),patch('cli_process.time.sleep'),patch('cli_process.time.monotonic',side_effect=[0,0,5]):
            with self.assertRaises(ValueError):wait_executable(124,'/owned/talker','789')


if __name__=='__main__':unittest.main()
