"""No fallback means zero attempts even when a UDP creation would fail."""
import unittest
from no_udp_contract import validate_policy,validate_counter
class NoUdp(unittest.TestCase):
    def setUp(self):
        self.counter={'abi':1,'pid':42,'total_calls':7,'ipv4_datagram_calls':0,'ipv6_datagram_calls':0,'datagram_successes':0,'failed_calls':1,'in_flight':0,'instrumentation_errors':0}
        self.policy={'mode':'selector','profile':None,'selector':'dsoftbus','discovery_range':'SYSTEM_DEFAULT'}
    def test_explicit_selector_and_live_counter(self):validate_policy(self.policy);validate_counter(self.counter,42)
    def test_failed_udp_attempt_is_failure(self):
        self.counter['ipv4_datagram_calls']=1
        with self.assertRaises(ValueError):validate_counter(self.counter,42)
    def test_ipv6_attempt_is_failure(self):
        self.counter['ipv6_datagram_calls']=1
        with self.assertRaises(ValueError):validate_counter(self.counter,42)
    def test_incomplete_instrumentation_is_failure(self):
        for key in ('in_flight','instrumentation_errors'):
            with self.assertRaises(ValueError):validate_counter({**self.counter,key:1},42)
    def test_absent_or_foreign_counter_is_failure(self):
        for change in ({'total_calls':0},{'pid':43},{'abi':2},{'failed_calls':99},{'total_calls':True}):
            with self.assertRaises(ValueError):validate_counter({**self.counter,**change},42)
    def test_implicit_policy_is_insufficient(self):
        for change in ({'selector':None},{'mode':'implicit'},{'selector':'udp'},{'profile':'ohos_dsoftbus'}):
            with self.assertRaises(ValueError):validate_policy({**self.policy,**change})
if __name__=='__main__':unittest.main()
