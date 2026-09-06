import unittest
from cli_graph_basic import oracle


class Oracle(unittest.TestCase):
    def test_topic_type_exact(self):
        self.assertTrue(oracle('cli:topic/type','std_msgs/msg/String\n','std_msgs/msg/String'))
    def test_topic_type_wrong(self):
        self.assertFalse(oracle('cli:topic/type','std_msgs/msg/Int32\n','std_msgs/msg/String'))
    def test_find_complete_set(self):
        self.assertTrue(oracle('cli:topic/find','/b\n/a\n',['/a','/b']))
    def test_find_ghost_is_rejected(self):
        self.assertFalse(oracle('cli:topic/find','/a\n/b\n/ghost\n',['/a','/b']))
    def test_service_exact_response(self):
        self.assertTrue(oracle('cli:service/call','response:\nexample_interfaces.srv.AddTwoInts_Response(sum=123)\n',123))
    def test_service_wrong_response(self):
        self.assertFalse(oracle('cli:service/call','response:\nexample_interfaces.srv.AddTwoInts_Response(sum=124)\n',123))


if __name__ == '__main__': unittest.main()
