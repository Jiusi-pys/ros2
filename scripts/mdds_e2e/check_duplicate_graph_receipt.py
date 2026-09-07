"""Adversarial checks against an actual same-name participant run."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
import cli_acceptance as a
from verify_cli_daemon import validate_report
from service_graph_receipt import emit

SOURCE=Path(sys.argv[1]).resolve();A,B=a.TARGET['board_serials']


class DuplicateReceipt(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='duplicate_receipt_');self.root=Path(self.temp.name)/SOURCE.name
        shutil.copytree(SOURCE,self.root,ignore=shutil.ignore_patterns('*.tar','*.py','mdds_broker_daemon','mdds_token_exec'))
        self.reports={b:json.loads((self.root/(b+'.cli.results.json')).read_bytes()) for b in (A,B)}
        self.nonce=(self.root/'nonce').read_text().strip()
    def tearDown(self):self.temp.cleanup()
    def check(self):
        for b,v in self.reports.items():validate_report(v,self.root,SOURCE.name,b,self.nonce)
        manifest_path=self.root/'cli_partial_manifest.json'
        if not manifest_path.exists():manifest_path=self.root/'cli_acceptance_manifest.json'
        manifest=json.loads(manifest_path.read_bytes())
        if emit(self.root,manifest,self.reports,SOURCE.name,self.nonce)!='graph:duplicate_node_names':raise ValueError('duplicate graph case not issued')
    def reject(self):
        with self.assertRaises((ValueError,KeyError,FileNotFoundError)):self.check()
    def alter_phase(self,board,index,change):
        value=self.reports[board]['duplicate_graph'][index];before=json.dumps(value);change(value)
        name='hidden_source.json' if index==0 else 'duplicate_survivor.json'
        (self.root/(board+'.'+name)).write_text(json.dumps(value))
        p=self.root/(board+'.ros.log');p.write_text(p.read_text().replace(before,json.dumps(value)))
    def test_original(self):self.check()
    def test_collapsed_same_names(self):self.alter_phase(A,0,lambda v:v['snapshot']['nodes'].pop());self.reject()
    def test_stale_owner_after_retirement(self):
        stale=self.reports[A]['duplicate_graph'][0]['snapshot']['services']
        self.alter_phase(A,1,lambda v:v['snapshot']['services'].update(stale));self.reject()
    def test_survivor_identity_changed(self):self.alter_phase(B,1,lambda v:v['snapshot']['gids']['B0'].__setitem__(4,v['snapshot']['gids']['B0'][4]^1));self.reject()
    def test_contexts_share_participant(self):
        self.alter_phase(A,0,lambda v:v['snapshot']['gids'].__setitem__('A1',v['snapshot']['gids']['A0']));self.reject()
    def test_missing_peer_data(self):self.alter_phase(B,1,lambda v:v['data']['B0']['received'].pop());self.reject()
    def test_service_reply_without_callback(self):
        p=self.root/(B+'.ros.log');p.write_text(p.read_text().replace('DUPLICATE_SERVICE_RX ','REMOVED '));self.reject()
    def test_phase_relabelled(self):self.alter_phase(A,1,lambda v:v.update(nonce='old'));self.reject()
    def test_retirement_barrier_stale(self):(self.root/(A+'.hidden_cli.go')).write_text('old');self.reject()
    def test_nonzero_native_exit(self):
        p=self.root/(B+'.ros.status.json');v=json.loads(p.read_bytes());v['returncode']=139;p.write_text(json.dumps(v));self.reject()
    def test_missing_native_terminal(self):
        p=self.root/(A+'.ros.log');p.write_text(p.read_text().replace('MDDS_CLI_TERMINAL ','REMOVED '));self.reject()
    def test_graph_case_not_frozen(self):
        p=self.root/('inputs_'+A+'.sha256');p.write_text('\n'.join(v for v in p.read_text().splitlines() if not v.endswith('  graph_case'))+'\n');self.reject()


if __name__=='__main__':unittest.main(argv=[sys.argv[0]])
