import unittest
from cli_lifecycle import oracle,recipe


class LifecycleOracle(unittest.TestCase):
    def test_nodes(self):self.assertTrue(oracle('/a\n/b\n',{'kind':'nodes','names':['/a','/b']}))
    def test_ordinary_node(self):self.assertFalse(oracle('/a\n/b\n/ordinary\n',{'kind':'nodes','names':['/a','/b']}))
    def test_count(self):self.assertTrue(oracle('2\n',{'kind':'count','value':2}))
    def test_state(self):self.assertTrue(oracle('active [3]\n',{'kind':'state','label':'active','id':3}))
    def test_wrong_id(self):self.assertFalse(oracle('active [2]\n',{'kind':'state','label':'active','id':3}))
    def test_set(self):self.assertTrue(oracle('Transitioning successful\n',{'kind':'set'}))
    def test_failed_set(self):self.assertFalse(oracle('Transitioning failed\n',{'kind':'set'}))
    def test_transitions(self):
        expected=recipe('/fixture','B')[3][3]
        raw='- configure [1]\n\tStart: unconfigured\n\tGoal: configuring\n- shutdown [5]\n\tStart: unconfigured\n\tGoal: shuttingdown\n'
        self.assertTrue(oracle(raw,expected));self.assertFalse(oracle(raw.replace('shuttingdown','finalized'),expected))
    def test_finalized(self):self.assertTrue(oracle('',{'kind':'transitions','items':[]}))


if __name__=='__main__':unittest.main()
