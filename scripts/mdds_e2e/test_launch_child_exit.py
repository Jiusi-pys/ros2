import unittest
from cli_process import native_exit_code


class LaunchChildExit(unittest.TestCase):
    def test_clean_native_exit(self):self.assertEqual(native_exit_code('[INFO] [talker-1]: process has finished cleanly [pid 123]\n',123),0)
    def test_native_crash_is_not_cli_success(self):self.assertEqual(native_exit_code("[ERROR] [talker-1]: process has died [pid 123, exit code -11, cmd '/owned/talker'].\n",123),-11)
    def test_missing_exit_is_unknown(self):self.assertIsNone(native_exit_code('[INFO] [talker-1]: process started with pid [123]\n',123))
    def test_other_pid_is_not_evidence(self):self.assertIsNone(native_exit_code('[INFO] [talker-1]: process has finished cleanly [pid 124]\n',123))
    def test_conflicting_exit_events_are_rejected(self):self.assertIsNone(native_exit_code('[INFO] [talker-1]: process has finished cleanly [pid 123]\n[ERROR] [talker-1]: process has died [pid 123, exit code -11, cmd x].\n',123))


if __name__=='__main__':unittest.main()
