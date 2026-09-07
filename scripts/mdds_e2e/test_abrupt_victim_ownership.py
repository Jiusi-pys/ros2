"""Never signal an unrelated or reused PID during failure injection."""
import unittest
from abrupt_victim_owner import owned_victim

ROOT='/data/local/tmp/ros2/.mdds-owned-runs/run'
ARGV=['python',ROOT+'/board_abrupt_victim.py',ROOT,'run','A','a'*32]

class Ownership(unittest.TestCase):
    def test_only_complete_identity_is_owned(self):
        value={'pid':123,'start':'456','state':'S','argv':ARGV,'broker_root':ROOT+'/brokers','libraries':[ROOT+'/lib/libmdds.so',ROOT+'/lib/librmw_mdds.so']}
        self.assertTrue(owned_victim(value,ROOT,ARGV,123,'456'))
        for change in ({'pid':124},{'start':'457'},{'state':'Z'},{'argv':ARGV[:-1]},{'broker_root':'/other/brokers'},{'libraries':[]},{'libraries':['/other/libmdds.so',ROOT+'/lib/librmw_mdds.so']}):
            self.assertFalse(owned_victim({**value,**change},ROOT,ARGV,123,'456'))

if __name__=='__main__':unittest.main()
