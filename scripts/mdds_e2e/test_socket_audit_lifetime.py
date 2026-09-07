"""Every preload image must close via an audited exec handoff or final exit."""
import json
import unittest
from socket_audit_lifetime import validate_lifetime

class Lifetime(unittest.TestCase):
    def setUp(self):
        self.pid=42;self.program='/data/python312-rk3588a/usr/bin/python3.12'
        self.zero={'abi':1,'pid':42,'total_calls':0,'ipv4_datagram_calls':0,'ipv6_datagram_calls':0,'datagram_successes':0,'failed_calls':0,'in_flight':0,'instrumentation_errors':0}
        self.final={**self.zero,'total_calls':5}
        self.lines=['MDDS_SOCKET_AUDIT_LOADED pid=42 abi=1 exe='+self.program,'MDDS_SOCKET_AUDIT_EXEC '+json.dumps(self.zero),'MDDS_SOCKET_AUDIT_LOADED pid=42 abi=1 exe='+self.program,'MDDS_SOCKET_AUDIT_FINAL '+json.dumps(self.final)]
    def check(self):validate_lifetime(self.lines,self.pid,self.program,self.final)
    def reject(self):
        with self.assertRaises(ValueError):self.check()
    def test_complete_images(self):self.check()
    def test_missing_handoff(self):self.lines.pop(1);self.reject()
    def test_unobserved_reset(self):self.lines.insert(2,self.lines[0]);self.reject()
    def test_wrong_order(self):self.lines[1],self.lines[2]=self.lines[2],self.lines[1];self.reject()
    def test_bootstrap_datagram_detected(self):self.lines[1]='MDDS_SOCKET_AUDIT_EXEC '+json.dumps({**self.zero,'total_calls':1,'ipv4_datagram_calls':1});self.reject()
    def test_wrong_exec_target(self):self.lines[2]+='_other';self.reject()
    def test_failed_exec(self):self.lines.insert(3,'MDDS_SOCKET_AUDIT_EXEC_FAILED pid=42');self.reject()
    def test_missing_final(self):self.lines.pop();self.reject()
    def test_native_single_image(self):validate_lifetime([self.lines[0],self.lines[-1]],self.pid,self.program,self.final,bootstrap_exec=False)
    def test_native_unclosed_reset(self):
        with self.assertRaises(ValueError):validate_lifetime(self.lines,self.pid,self.program,self.final,bootstrap_exec=False)
    def test_native_missing_final(self):
        with self.assertRaises(ValueError):validate_lifetime([self.lines[0]],self.pid,self.program,self.final,bootstrap_exec=False)
if __name__=='__main__':unittest.main()
