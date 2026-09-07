"""Adversaries for graph withdrawal, local continuity and recovered RPC."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from cli_acceptance import TARGET
from verify_remote_cycle import check
SOURCE=Path(sys.argv[1]).resolve();A,B=TARGET['board_serials']

class CycleGraphReceipt(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)/SOURCE.name
        shutil.copytree(SOURCE,self.root,ignore=shutil.ignore_patterns('*.tar','*.py','mdds_broker_daemon','mdds_token_exec'))
    def tearDown(self):self.temp.cleanup()
    def check(self):
        value=check(self.root)
        if 'graph_cycle' not in value:raise ValueError('graph checks were disabled')
    def reject(self):
        with self.assertRaises((ValueError,FileNotFoundError)):self.check()
    def record(self,stage,operation):
        p=self.root/(A+'.reconnect.graph_'+stage+'.json');v=json.loads(p.read_bytes());old=json.dumps(v);operation(v);p.write_text(json.dumps(v))
        raw=self.root/(A+'.ros.log');raw.write_text(raw.read_text().replace('CYCLE_GRAPH_'+stage.upper()+' '+old,'CYCLE_GRAPH_'+stage.upper()+' '+json.dumps(v)))
    def test_original(self):self.check()
    def test_remote_endpoints_must_withdraw(self):
        before=json.loads((self.root/(A+'.reconnect.graph_ready.json')).read_bytes())['snapshot']
        self.record('paused',lambda v:v.update(snapshot=before));self.reject()
    def test_local_endpoint_must_survive(self):self.record('paused',lambda v:v['snapshot']['endpoints'].pop());self.reject()
    def test_catalog_must_match(self):self.record('paused',lambda v:v['snapshot']['catalog'].pop());self.reject()
    def test_restored_gid_must_match(self):self.record('restored',lambda v:v['snapshot']['endpoints'][0]['gid'].__setitem__(0,v['snapshot']['endpoints'][0]['gid'][0]^1));self.reject()
    def test_restored_type_hash_must_match(self):self.record('restored',lambda v:v['snapshot']['endpoints'][0].update(hash='wrong'));self.reject()
    def test_local_receipt_required(self):self.record('paused',lambda v:v.update(local_received=[]));self.reject()
    def test_recovered_rpc_required(self):self.record('restored',lambda v:v.update(sum=v['sum']+1));self.reject()
    def test_server_callback_required(self):
        p=self.root/(B+'.ros.log');p.write_text(p.read_text().replace('CYCLE_RPC_SERVER ','REMOVED '));self.reject()
    def test_cleanup_required(self):self.record('done',lambda v:v.update(local_probe_removed=False));self.reject()
    def test_feature_flag_cannot_disable_checks(self):(self.root/'cycle_graph.enabled').unlink();self.reject()

if __name__=='__main__':unittest.main(argv=[sys.argv[0]])
