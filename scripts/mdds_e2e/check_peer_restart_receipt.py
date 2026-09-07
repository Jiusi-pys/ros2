"""Reject incomplete or stale peer replacement evidence."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from cli_acceptance import TARGET
from verify_remote_cycle import check
from service_graph_receipt import emit
SOURCE=Path(sys.argv[1]).resolve();A,B=TARGET['board_serials']

class PeerRestartReceipt(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)/SOURCE.name
        shutil.copytree(SOURCE,self.root,ignore=shutil.ignore_patterns('*.tar','*.py','mdds_broker_daemon','mdds_token_exec'))
    def tearDown(self):self.temp.cleanup()
    def check(self):
        value=check(self.root)
        if 'peer_restart' not in value or 'graph_cycle' not in value:raise ValueError('required recovery checks disabled')
        manifest=json.loads((self.root/'cli_acceptance_manifest.json').read_bytes());manifest['run_id']=SOURCE.name
        reports={b:{'peer_restart':json.loads((self.root/(b+'.reconnect.peer_final.json')).read_bytes())} for b in (A,B)}
        if emit(self.root,manifest,reports,SOURCE.name,(self.root/'nonce').read_text().strip())!='graph:reconnect':raise ValueError('case not accepted')
    def reject(self):
        with self.assertRaises((ValueError,KeyError,FileNotFoundError)):self.check()
    def phase(self,stage,operation):
        p=self.root/(A+'.reconnect.peer_'+stage+'.json');v=json.loads(p.read_bytes());old=json.dumps(v);operation(v);p.write_text(json.dumps(v))
        log=self.root/(A+'.ros.log');log.write_text(log.read_text().replace('PEER_GRAPH_'+stage.upper()+' '+old,'PEER_GRAPH_'+stage.upper()+' '+json.dumps(v)))
    def test_original(self):self.check()
    def test_old_gid_cannot_return(self):
        old=json.loads((self.root/(A+'.reconnect.peer_before.json')).read_bytes())['snapshot']['endpoints'][0]['gid']
        self.phase('restored',lambda v:v['snapshot']['endpoints'][0].update(gid=old));self.reject()
    def test_missing_action_endpoint(self):self.phase('restored',lambda v:v['snapshot']['endpoints'].pop());self.reject()
    def test_qos_changed_on_restart(self):self.phase('restored',lambda v:v['snapshot']['endpoints'][0]['qos'].update(depth=999));self.reject()
    def test_local_old_peer_must_disappear(self):
        old=json.loads((self.root/(A+'.reconnect.peer_paused.json')).read_bytes())['snapshot']
        self.phase('old_gone',lambda v:v.update(snapshot=old));self.reject()
    def test_new_generation_must_be_observed_while_paused(self):(self.root/(A+'.reconnect.peer_new_local.json')).unlink();self.reject()
    def test_final_ghost(self):self.phase('final',lambda v:v['snapshot']['nodes'].append(['ghost','/wrong','/']));self.reject()
    def test_old_exit_cannot_be_relabelled(self):
        p=self.root/(A+'.reconnect.peer_1.status.json');v=json.loads(p.read_bytes());v['returncode']=0;p.write_text(json.dumps(v));self.reject()
    def test_recovered_process_must_exit_cleanly(self):
        p=self.root/(B+'.reconnect.peer_2.status.json');v=json.loads(p.read_bytes());v['returncode']=139;p.write_text(json.dumps(v));self.reject()
    def test_owned_kill_identity_required(self):
        p=self.root/(A+'.peer_kill.json');v=json.loads(p.read_bytes());v['identity']['libraries']=[];p.write_text(json.dumps(v));self.reject()
    def test_new_payload_callback_required(self):
        p=self.root/(A+'.peer_2.log');p.write_text(p.read_text().replace('RESTART_PEER_RX ','REMOVED '));self.reject()
    def test_new_rpc_callback_required(self):
        p=self.root/(B+'.peer_2.log');p.write_text(p.read_text().replace('RESTART_PEER_SERVER ','REMOVED '));self.reject()
    def test_new_process_provenance_required(self):
        p=self.root/(A+'.reconnect.peer_2.created.json');v=json.loads(p.read_bytes());v['provenance']['owned_udp_sockets']=[{'inode':'fake'}];p.write_text(json.dumps(v));self.reject()
    def test_main_context_restoration_is_required(self):(self.root/'cycle_graph.enabled').unlink();self.reject()
    def test_case_declaration_is_required(self):(self.root/'graph_case').unlink();self.reject()
    def test_native_case_terminal_is_required(self):
        p=self.root/(A+'.ros.log');p.write_text(p.read_text().replace('MDDS_CLI_TERMINAL ','REMOVED '));self.reject()
    def test_worker_identity_must_survive(self):(self.root/(A+'.peer_worker.child.final.pid')).write_text('changed');self.reject()

if __name__=='__main__':unittest.main(argv=[sys.argv[0]])
