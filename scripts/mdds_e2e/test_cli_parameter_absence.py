import copy
import unittest
from cli_acceptance import validate_parameter_absence


class ParameterAbsence(unittest.TestCase):
    def setUp(self):
        self.execution={'argv':['ros2','param','get','/fixture','ephemeral'],'board_serial':'board','returncode':1,'expected_failure':'parameter_not_set'}
        self.earlier=[{'argv':['ros2','param','delete','/fixture','ephemeral'],'board_serial':'board','returncode':0}]
        self.log='MDDS_CLI_STDOUT_BEGIN\n\nMDDS_CLI_STDOUT_END\nMDDS_CLI_STDERR_BEGIN\nParameter not set\n\nMDDS_CLI_STDERR_END\n'
    def validate(self):validate_parameter_absence(self.execution,self.earlier,self.log)
    def reject(self):
        with self.assertRaises(ValueError):self.validate()
    def test_real_absence(self):self.validate()
    def test_no_delete(self):self.earlier=[];self.reject()
    def test_wrong_node(self):self.earlier[0]['argv'][3]='/other';self.reject()
    def test_wrong_parameter(self):self.earlier[0]['argv'][4]='other';self.reject()
    def test_wrong_board(self):self.earlier[0]['board_serial']='other';self.reject()
    def test_failed_delete(self):self.earlier[0]['returncode']=1;self.reject()
    def test_other_error(self):self.log=self.log.replace('Parameter not set','Node not found');self.reject()
    def test_timeout(self):self.execution['returncode']=124;self.reject()
    def test_zero_exit(self):self.execution['returncode']=0;self.reject()


if __name__=='__main__':unittest.main()
