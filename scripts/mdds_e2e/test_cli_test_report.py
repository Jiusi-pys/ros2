import unittest
from cli_test_report import validate_xml


class LaunchTestingReport(unittest.TestCase):
    def setUp(self):
        self.xml='<testsuites tests="3" failures="0" errors="0"><testsuite name="mdds_cli_fixture.peer_talker_test.launch_tests" tests="3" failures="0" errors="0" skipped="0"><testcase classname="mdds_cli_fixture.TestPeerProcess" name="test_native_publication"/><testcase classname="mdds_cli_fixture.TestPeerProcess" name="test_peer_exchange"/><testcase classname="mdds_cli_fixture.TestPeerShutdown" name="test_native_exit"/></testsuite></testsuites>'
    def test_real_expected_tests(self):self.assertEqual(validate_xml(self.xml)['tests'],3)
    def test_empty_report(self):
        with self.assertRaises(ValueError):validate_xml('<testsuites tests="0" failures="0" errors="0"/>')
    def test_hidden_failure(self):
        with self.assertRaises(ValueError):validate_xml(self.xml.replace('name="test_native_exit"/>','name="test_native_exit"><failure/></testcase>'))
    def test_skipped_case(self):
        with self.assertRaises(ValueError):validate_xml(self.xml.replace('skipped="0"','skipped="1"'))
    def test_duplicate_case(self):
        with self.assertRaises(ValueError):validate_xml(self.xml.replace('name="test_peer_exchange"','name="test_native_publication"'))
    def test_forged_count(self):
        with self.assertRaises(ValueError):validate_xml(self.xml.replace('tests="3"','tests="4"',1))
    def test_root_skip_cannot_contradict_success(self):
        with self.assertRaises(ValueError):validate_xml(self.xml.replace('<testsuites ','<testsuites skipped="1" ',1))


if __name__=='__main__':unittest.main()
