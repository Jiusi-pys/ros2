"""Reject missing SDK generations, residual resources and failed ROS recovery."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from cli_acceptance import TARGET
from verify_remote_cycle import check
SOURCE=Path(sys.argv[1]).resolve();A=TARGET['board_serials'][0]

class RemoteCycleReceipt(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)/SOURCE.name
        shutil.copytree(SOURCE,self.root,ignore=shutil.ignore_patterns('*.tar','*.py','mdds_broker_daemon','mdds_token_exec'))
    def tearDown(self):self.temp.cleanup()
    def reject(self):
        with self.assertRaises(ValueError):check(self.root)
    def record(self,name,change):
        p=self.root/(A+'.reconnect.'+name+'.json');value=json.loads(p.read_bytes());old=value.copy();change(value);p.write_text(json.dumps(value))
        log=self.root/(A+'.daemon.log');lines=log.read_text().splitlines()
        for i,line in enumerate(lines):
            for prefix in ('RECONNECT_STAGE ','RECONNECT_SDK_STOP '):
                if line.startswith(prefix) and json.loads(line[len(prefix):])==old:lines[i]=prefix+json.dumps(value)
        log.write_text('\n'.join(lines)+'\n')
    def test_original(self):self.assertTrue(check(self.root)['passed'])
    def test_stop_must_drain(self):self.record('sdk_stop_1',lambda v:v.update(pending_retirements=1));self.reject()
    def test_local_clients_must_survive(self):self.record('paused',lambda v:v.update(connections=0));self.reject()
    def test_fresh_generation_required(self):self.record('restored',lambda v:v.update(generation=1));self.reject()
    def test_actual_second_bind_required(self):
        p=self.root/(A+'.daemon.log');lines=p.read_text().splitlines();seen=False
        for i,line in enumerate(lines):
            if line.startswith('[mdds/dsoftbus] OnBind('):
                if seen:lines[i]='REMOVED '+line
                seen=True
        p.write_text('\n'.join(lines)+'\n');self.reject()
    def test_new_nonce_required(self):
        p=self.root/(A+'.daemon.log');p.write_text('\n'.join(line for line in p.read_text().splitlines() if not line.startswith('RECONNECT_RECEIVE_NONCE generation=2'))+'\n');self.reject()
    def test_raw_stage_required(self):
        p=self.root/(A+'.daemon.log');p.write_text(p.read_text().replace('RECONNECT_STAGE ','REMOVED '));self.reject()
    def test_ros_recovery_must_pass(self):
        p=self.root/'host_report.json';v=json.loads(p.read_bytes());v['passed']=False;p.write_text(json.dumps(v));self.reject()

if __name__=='__main__':unittest.main(argv=[sys.argv[0]])
