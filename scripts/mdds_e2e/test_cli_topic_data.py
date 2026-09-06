import copy
import unittest
from cli_graph_basic import oracle


def endpoint(kind, name, gid):
    return {'Node name':name,'Node namespace':'/fixture','Topic type':'std_msgs/msg/String',
            'Topic type hash':'RIHS01_'+'1'*64,'Endpoint type':kind,'GID':gid,
            'Reliability':'RELIABLE','History (Depth)':'KEEP_LAST (32)','Durability':'VOLATILE',
            'Lifespan':'Infinite','Deadline':'Infinite','Liveliness':'AUTOMATIC','Liveliness lease duration':'Infinite'}


def info_text(expected):
    text='Type: std_msgs/msg/String\n\nPublisher count: 1\n\n'
    for key, item in expected['publisher'].items():text+=key+': '+item+'\n'
    text+='\nSubscription count: 1\n\n'
    for key, item in expected['subscription'].items():text+=key+': '+item+'\n'
    return text


class TopicDataOracle(unittest.TestCase):
    def setUp(self):
        self.expected={'publisher':endpoint('PUBLISHER','alpha_B','.'.join(['01']*16)),
                       'subscription':endpoint('SUBSCRIPTION','alpha_A','.'.join(['02']*16))}
    def test_verbose_complete(self):self.assertTrue(oracle('cli:topic/info',info_text(self.expected),self.expected))
    def test_wrong_gid(self):
        wrong=copy.deepcopy(self.expected);wrong['publisher']['GID']='00'
        self.assertFalse(oracle('cli:topic/info',info_text(wrong),self.expected))
    def test_wrong_qos(self):
        wrong=copy.deepcopy(self.expected);wrong['subscription']['History (Depth)']='KEEP_LAST (1)'
        self.assertFalse(oracle('cli:topic/info',info_text(wrong),self.expected))
    def test_publish_exact(self):self.assertTrue(oracle('cli:topic/pub','publishing #1: std_msgs.msg.Int32(data=123)\n',123))
    def test_publish_wrong(self):self.assertFalse(oracle('cli:topic/pub','publishing #1: std_msgs.msg.Int32(data=124)\n',123))
    def test_echo_field_exact(self):self.assertTrue(oracle('cli:topic/echo','123\n---\n',123))
    def test_echo_field_wrong(self):self.assertFalse(oracle('cli:topic/echo','124\n---\n',123))


if __name__=='__main__':unittest.main()
