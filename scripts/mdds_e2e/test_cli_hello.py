import unittest
from cli_hello import summary


class HelloSummary(unittest.TestCase):
    def setUp(self):
        self.expected={'topic':'/hello/test','peer_id':'B_nonce','group':'239.255.77.7','port':50123}
        self.text='MULTIMACHINE COMMUNICATION SUMMARY\nTopic: /hello/test, Published Msg Count: 10\nSubscribed from:\n Hostname Msg Count /1.0s\n B_nonce 9\nMulticast Group/Port: 239.255.77.7/50123, Sent Msg Count: 10\nReceived from:\n Hostname Msg Count /1.0s\n B_nonce 8\n'+'-'*60+'\n'
    def test_both_lanes(self):self.assertIsNotNone(summary(self.text,self.expected))
    def test_udp_cannot_substitute_ros(self):self.assertIsNone(summary(self.text.replace('B_nonce 9','B_nonce 0'),self.expected))
    def test_ros_cannot_substitute_udp(self):self.assertIsNone(summary(self.text.replace('B_nonce 8','B_nonce 0'),self.expected))
    def test_wrong_identity(self):self.assertIsNone(summary(self.text.replace('B_nonce','stale'),self.expected))
    def test_wrong_topic(self):self.assertIsNone(summary(self.text.replace('/hello/test','/wrong'),self.expected))


if __name__=='__main__':unittest.main()
