"""Mutate complete native no-fallback evidence; no weaker proof may pass."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from cli_acceptance import TARGET
from verify_no_udp_fallback import emit
SOURCE=Path(sys.argv[1]).resolve();A,B=TARGET['board_serials']
class NoUdpReceipt(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)/SOURCE.name
        shutil.copytree(SOURCE,self.root,ignore=shutil.ignore_patterns('*.py','*.tar','mdds_token_exec'))
    def tearDown(self):self.temp.cleanup()
    def reject(self):
        with self.assertRaises((ValueError,KeyError,FileNotFoundError)):emit(self.root)
    def log(self,board,role,old,new):
        p=self.root/(board+'.'+role+'.log');p.write_text(p.read_text().replace(old,new))
    def counter(self,board,role,change):
        p=self.root/(board+'.'+role+'.log');lines=p.read_text().splitlines()
        for i,line in enumerate(lines):
            if line.startswith('MDDS_SOCKET_AUDIT_FINAL '):
                v=json.loads(line.removeprefix('MDDS_SOCKET_AUDIT_FINAL '));change(v);lines[i]='MDDS_SOCKET_AUDIT_FINAL '+json.dumps(v)
        p.write_text('\n'.join(lines)+'\n')
    def record(self,board,name,prefix,role,change):
        p=self.root/(board+'.'+name);v=json.loads(p.read_bytes());old=prefix+json.dumps(v);change(v);p.write_text(json.dumps(v));self.log(board,role,old,prefix+json.dumps(v))
    def test_original(self):emit(self.root)
    def test_failed_udp_attempt(self):self.counter(A,'ros',lambda v:v.update(ipv4_datagram_calls=1));self.reject()
    def test_ipv6_attempt(self):self.counter(B,'daemon',lambda v:v.update(ipv6_datagram_calls=1));self.reject()
    def test_counter_not_attached(self):self.counter(A,'daemon',lambda v:v.update(total_calls=0));self.reject()
    def test_inflight_rejected(self):self.counter(B,'ros',lambda v:v.update(in_flight=1));self.reject()
    def test_instrument_failure(self):self.counter(A,'daemon',lambda v:v.update(instrumentation_errors=1));self.reject()
    def test_native_final_required(self):self.log(A,'daemon','MDDS_SOCKET_AUDIT_FINAL ','REMOVED ');self.reject()
    def test_exec_handoff_required(self):self.log(B,'ros','MDDS_SOCKET_AUDIT_EXEC ','REMOVED ');self.reject()
    def test_shutdown_must_be_observed(self):self.log(A,'ros','NO_UDP_ROS_STOPPED ','REMOVED ');self.reject()
    def test_shutdown_must_precede_audit_end(self):
        p=self.root/(A+'.ros.log');lines=p.read_text().splitlines();stop=next(v for v in lines if v.startswith('NO_UDP_ROS_STOPPED '));lines.remove(stop);lines.append(stop);p.write_text('\n'.join(lines)+'\n');self.reject()
    def test_sdk_must_really_disconnect(self):self.record(A,'reconnect.paused.json','RECONNECT_STAGE ','daemon',lambda v:v.update(channels=1));self.reject()
    def test_peer_service_must_disappear(self):self.record(B,'reconnect.graph_paused.json','CYCLE_GRAPH_PAUSED ','ros',lambda v:v.update(remote_service_ready=True));self.reject()
    def test_restored_rpc_must_match(self):self.record(A,'reconnect.graph_restored.json','CYCLE_GRAPH_RESTORED ','ros',lambda v:v.update(sum=0));self.reject()
    def test_frozen_mode_required(self):(self.root/(A+'.no_udp.enabled')).write_text('wrong');self.reject()
    def test_positive_instrument_control_required(self):
        meta=json.loads((self.root/'audit_control.json').read_bytes());p=self.root/meta['directory']/(B+'.positive.log');p.write_text(p.read_text().replace('MDDS_SOCKET_AUDIT_FINAL ','REMOVED '));self.reject()
    def test_case_terminal_required(self):self.log(B,'daemon','MDDS_CLI_TERMINAL ','REMOVED ');self.reject()
    def test_runtime_positive_required(self):
        meta=json.loads((self.root/'audit_control.json').read_bytes());directory=self.root/meta['directory'];p=directory/(A+'.positive.result.json');v=json.loads(p.read_bytes());old=json.dumps(v);v.pop('numpy_control');v.pop('preload');p.write_text(json.dumps(v));log=directory/(A+'.positive.log');log.write_text(log.read_text().replace(old,json.dumps(v)));self.reject()
if __name__=='__main__':unittest.main(argv=[sys.argv[0]])
