import unittest
from cli_bag import oracle
from bag_contract import topic


class BagInfoOracle(unittest.TestCase):
    def setUp(self):
        self.expected={'storage':'sqlite3','source_role':'B','run_id':'test'}
        self.text='Storage id: sqlite3\nDuration: 0.400s\nMessages: 10\nTopic information: '+ '\n'.join('Topic: '+topic('test','B','sqlite3',kind)+' | Type: std_msgs/msg/String | Count: 5 | Serialization Format: cdr' for kind in ('main','noise'))+'\n'
    def test_info(self):self.assertTrue(oracle('cli:bag/info',self.text,self.expected))
    def test_wrong_storage(self):self.assertFalse(oracle('cli:bag/info',self.text.replace('Storage id: sqlite3','Storage id: mcap'),self.expected))
    def test_wrong_count(self):self.assertFalse(oracle('cli:bag/info',self.text.replace('Messages: 10','Messages: 9'),self.expected))
    def test_wrong_type(self):self.assertFalse(oracle('cli:bag/info',self.text.replace('std_msgs/msg/String','std_msgs/msg/Int32'),self.expected))
    def test_missing_topic(self):self.assertFalse(oracle('cli:bag/info',self.text.splitlines()[0]+'\n',self.expected))


if __name__=='__main__':unittest.main()
