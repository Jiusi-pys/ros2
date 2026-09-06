import copy
import unittest
import yaml
from cli_service_events import events_match,KINDS


class ServiceEvents(unittest.TestCase):
    def setUp(self):
        self.expected={'service':'/fixture/serve','a':123,'b':17,'sum':140,'client_gid':list(range(16)),'sequence_number':7}
        self.events=[{'info':{'event_type':kind,'stamp':{'sec':1,'nanosec':2},'client_gid':list(range(16)),'sequence_number':7},
                      'request':[{'a':123,'b':17}] if kind.startswith('REQUEST') else [],
                      'response':[{'sum':140}] if kind.startswith('RESPONSE') else []} for kind in KINDS]
    def valid(self):return events_match(yaml.safe_dump_all(self.events),self.expected)
    def test_complete(self):self.assertTrue(self.valid())
    def test_reordered_sources(self):self.events.reverse();self.assertTrue(self.valid())
    def test_wrong_gid(self):self.events[1]['info']['client_gid'][0]=99;self.assertFalse(self.valid())
    def test_wrong_sequence(self):self.events[2]['info']['sequence_number']=8;self.assertFalse(self.valid())
    def test_wrong_request(self):self.events[0]['request'][0]['a']=0;self.assertFalse(self.valid())
    def test_wrong_response(self):self.events[2]['response'][0]['sum']=0;self.assertFalse(self.valid())
    def test_missing_event(self):self.events.pop();self.assertFalse(self.valid())
    def test_duplicate_event(self):self.events.append(copy.deepcopy(self.events[0]));self.assertFalse(self.valid())
    def test_empty_gid(self):
        self.expected['client_gid']=[0]*16
        for event in self.events:event['info']['client_gid']=[0]*16
        self.assertFalse(self.valid())
    def test_bad_stamp(self):self.events[0]['info']['stamp']['nanosec']=1000000000;self.assertFalse(self.valid())


if __name__=='__main__':unittest.main()
