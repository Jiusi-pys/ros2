import unittest
from cli_action import oracle,recipe,RESULT,FEEDBACK,TYPE


def output():
    text='Goal accepted with ID: '+'12'*16+'\n'
    for seq in FEEDBACK:text+='Feedback:\n    partial_sequence:\n'+''.join('- '+str(n)+'\n' for n in seq)
    return text+'Result:\n    sequence:\n'+''.join('- '+str(n)+'\n' for n in RESULT)+'Goal finished with status: SUCCEEDED\n'


class ActionOracle(unittest.TestCase):
    def test_list(self):self.assertTrue(oracle('cli:action/list','/a ['+TYPE+']\n',['/a ['+TYPE+']']))
    def test_count(self):self.assertTrue(oracle('cli:action/list','2\n',2))
    def test_type(self):self.assertTrue(oracle('cli:action/type',TYPE+'\n',TYPE))
    def test_info(self):
        expected=recipe('/fixture','B')[3][3]
        raw='Action: /fixture/B/cli_action\nAction clients: 1\n    /fixture/beta_A ['+TYPE+']\nAction servers: 1\n    /fixture/alpha_B ['+TYPE+']\n'
        self.assertTrue(oracle('cli:action/info',raw,expected))
        self.assertFalse(oracle('cli:action/info',raw.replace('beta_A','alpha_A'),expected))
    def test_goal(self):self.assertTrue(oracle('cli:action/send_goal',output(),RESULT))
    def test_wrong_result(self):self.assertFalse(oracle('cli:action/send_goal',output().replace('    sequence:\n- 0','    sequence:\n- 9'),RESULT))
    def test_wrong_status(self):self.assertFalse(oracle('cli:action/send_goal',output().replace('SUCCEEDED','ABORTED'),RESULT))
    def test_missing_feedback(self):self.assertFalse(oracle('cli:action/send_goal',output().replace('partial_sequence','other'),RESULT))
    def test_missing_uuid(self):self.assertFalse(oracle('cli:action/send_goal',output().replace('12'*16,'00'*16),RESULT))
    def test_extra_goal(self):self.assertFalse(oracle('cli:action/send_goal',output()+output(),RESULT))


if __name__=='__main__':unittest.main()
