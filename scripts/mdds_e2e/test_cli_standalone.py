import unittest
from cli_standalone import native_matches,recipe


class StandaloneIdentity(unittest.TestCase):
    def setUp(self):
        self.root='/data/local/tmp/ros2/.mdds-owned-runs/test'
        self.expected=recipe('/ros_broker_test','B')[0][3]
        exe=self.root+'/component_prefix/lib/rclcpp_components/component_container'
        self.record={'pid':124,'start':'789','parent_pid':123,'process_group':123,'executable':exe,'argv':[exe,'--ros-args','-r','__node:='+self.expected['container']]}
    def test_owned(self):self.assertTrue(native_matches(self.record,self.root,123,self.expected))
    def test_foreign_parent(self):self.record['parent_pid']=9;self.assertFalse(native_matches(self.record,self.root,123,self.expected))
    def test_foreign_group(self):self.record['process_group']=9;self.assertFalse(native_matches(self.record,self.root,123,self.expected))
    def test_wrong_binary(self):self.record['executable']='/other';self.assertFalse(native_matches(self.record,self.root,123,self.expected))
    def test_wrong_args(self):self.record['argv'][-1]='__node:=other';self.assertFalse(native_matches(self.record,self.root,123,self.expected))
    def test_missing_start(self):self.record['start']='';self.assertFalse(native_matches(self.record,self.root,123,self.expected))


if __name__=='__main__':unittest.main()
