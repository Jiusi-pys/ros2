import unittest
from cli_doctor import oracle,REPORTS


class DoctorOutput(unittest.TestCase):
    def test_all_checks(self):self.assertTrue(oracle('All 5 checks passed\n',{'kind':'checks'}))
    def test_missing_check(self):self.assertFalse(oracle('All 3 checks passed\n',{'kind':'checks'}))
    def test_failed_check(self):self.assertFalse(oracle('1/5 check(s) failed\n',{'kind':'checks'}))
    def test_report_identity(self):
        text='\n'.join('   '+v for v in REPORTS)+'\nmiddleware name : rmw_mdds\ndistribution name : jazzy\ndistribution type : ros2\ndistribution status : active\n'
        self.assertTrue(oracle(text,{'kind':'report'}))
        self.assertFalse(oracle(text.replace('rmw_mdds','rmw_fastrtps_cpp'),{'kind':'report'}))
        self.assertFalse(oracle(text.replace('PACKAGE VERSIONS',''),{'kind':'report'}))


if __name__=='__main__':unittest.main()
