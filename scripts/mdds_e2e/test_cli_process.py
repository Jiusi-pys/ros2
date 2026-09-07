import unittest
from cli_process import native_matches,recipe


class ProcessIdentity(unittest.TestCase):
    def setUp(self):
        self.root='/data/local/tmp/ros2/.mdds-owned-runs/test'
        self.expected=recipe('/ros_broker_test','B')[0][3]
        exe=self.root+'/execution_prefix/lib/demo_nodes_cpp/talker'
        self.record={'pid':124,'start':'789','parent_pid':123,'process_group':123,'executable':exe,'argv':[exe]+self.expected['native_args']}
    def test_owned(self):self.assertTrue(native_matches(self.record,self.root,123,self.expected))
    def test_wrong_parent(self):self.record['parent_pid']=1;self.assertFalse(native_matches(self.record,self.root,123,self.expected))
    def test_wrong_group(self):self.record['process_group']=124;self.assertFalse(native_matches(self.record,self.root,123,self.expected))
    def test_shared_prefix_binary(self):self.record['executable']='/data/local/tmp/ros2/Lib/demo_nodes_cpp/talker';self.assertFalse(native_matches(self.record,self.root,123,self.expected))
    def test_wrong_remapping(self):self.record['argv'][-1]='chatter:=/wrong';self.assertFalse(native_matches(self.record,self.root,123,self.expected))
    def test_missing_start(self):self.record['start']='';self.assertFalse(native_matches(self.record,self.root,123,self.expected))


if __name__=='__main__':unittest.main()
