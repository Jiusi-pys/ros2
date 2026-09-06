import copy
import unittest
from cli_daemon_guard import DAEMON_ARGS, owned


class DaemonOwnership(unittest.TestCase):
    def setUp(self):
        self.root='/data/local/tmp/ros2/.mdds-owned-runs/run_a'
        self.record={'pid':123,'start':'1234','state':'S','argv':['python3.12']+DAEMON_ARGS,
                     'broker_root':self.root+'/brokers',
                     'libraries':[self.root+'/lib/libmdds.so', self.root+'/lib/librmw_mdds.so']}
    def test_owned(self): self.assertTrue(owned(self.record,self.root))
    def test_foreign_root(self): self.assertFalse(owned(self.record,self.root+'x'))
    def test_missing_rmw(self):
        self.record['libraries'].pop();self.assertFalse(owned(self.record,self.root))
    def test_foreign_command(self):
        self.record['argv'][2]='other';self.assertFalse(owned(self.record,self.root))
    def test_wrong_domain(self):
        self.record['argv'][6]='176';self.assertFalse(owned(self.record,self.root))
    def test_no_start(self):
        self.record['start']='';self.assertFalse(owned(self.record,self.root))
    def test_foreign_mapping(self):
        self.record['libraries'].append('/foreign/libmdds.so');self.assertFalse(owned(self.record,self.root))


if __name__ == '__main__':unittest.main()
