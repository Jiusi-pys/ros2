"""Adversarial validation of actual victim-death receipts."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
import cli_acceptance as a
from verify_cli_daemon import validate_report
from service_graph_receipt import emit
from abrupt_graph_contract import base,BOUND_NS

SOURCE=Path(sys.argv[1]).resolve();A,B=a.TARGET['board_serials']

class AbruptReceipt(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='abrupt_receipt_');self.root=Path(self.temp.name)/SOURCE.name
        shutil.copytree(SOURCE,self.root,ignore=shutil.ignore_patterns('*.tar','*.py','mdds_broker_daemon','mdds_token_exec'))
        self.reports={b:json.loads((self.root/(b+'.cli.results.json')).read_bytes()) for b in (A,B)}
        self.nonce=(self.root/'nonce').read_text().strip()
    def tearDown(self):self.temp.cleanup()
    def check(self):
        for b,v in self.reports.items():validate_report(v,self.root,SOURCE.name,b,self.nonce)
        p=self.root/'cli_partial_manifest.json'
        if not p.exists():p=self.root/'cli_acceptance_manifest.json'
        if emit(self.root,json.loads(p.read_bytes()),self.reports,SOURCE.name,self.nonce)!='graph:abrupt_exit':raise ValueError('abrupt case not issued')
    def reject(self):
        with self.assertRaises((ValueError,KeyError,FileNotFoundError)):self.check()
    def parent(self,operation):
        value=self.reports[A]['abrupt_graph'];old=json.dumps(value);phases={k:json.dumps(value[k]) for k in ('before','after')};operation(value)
        (self.root/(A+'.abrupt_graph.json')).write_text(json.dumps(value))
        p=self.root/(A+'.ros.log');raw=p.read_text().replace(old,json.dumps(value))
        for key,before in phases.items():
            (self.root/(A+'.abrupt_'+key+'.json')).write_text(json.dumps(value[key]));raw=raw.replace('ABRUPT_'+key.upper()+' '+before,'ABRUPT_'+key.upper()+' '+json.dumps(value[key]))
        p.write_text(raw)
    def victim(self,filename,operation):
        p=self.root/(A+'.victim.'+filename+'.json');v=json.loads(p.read_bytes());old=json.dumps(v);operation(v);p.write_text(json.dumps(v))
        log=self.root/(A+'.victim.log');log.write_text(log.read_text().replace(old,json.dumps(v)))
    def test_original(self):self.check()
    def test_graceful_exit_is_not_sigkill(self):self.victim('status',lambda v:v.update(returncode=0));self.reject()
    def test_wrong_signal(self):self.victim('status',lambda v:v.update(signal=15));self.reject()
    def test_reused_pid(self):self.victim('status',lambda v:v.update(start=str(int(v['start'])+1)));self.reject()
    def test_foreign_library(self):self.victim('status',lambda v:v['identity'].update(libraries=['/foreign/libmdds.so']));self.reject()
    def test_wrong_argv(self):self.victim('status',lambda v:v['argv'].pop());self.reject()
    def test_kill_before_observer_armed(self):
        arm=json.loads((self.root/(A+'.abrupt_armed.json')).read_bytes())
        self.victim('status',lambda v:v.update(started_ns=arm['armed_ns']-1));self.reject()
    def test_unbounded_removal(self):self.parent(lambda v:v['after'].update(removed_ns=v['after']['armed_ns']+BOUND_NS+1));self.reject()
    def test_dead_service_remains(self):self.parent(lambda v:v['after']['snapshot']['counts']['B:victim'].update(servers=1));self.reject()
    def test_live_client_name_must_remain(self):self.parent(lambda v:v['after']['snapshot']['services'].pop(base(SOURCE.name,'B','victim')+'/serve'));self.reject()
    def test_survivor_gid_changes(self):self.parent(lambda v:v['after']['snapshot']['gids']['A:survivor'].__setitem__(0,v['after']['snapshot']['gids']['A:survivor'][0]^1));self.reject()
    def test_survivor_post_kill_data_missing(self):self.parent(lambda v:v['after']['data']['received']['survivor'].pop());self.reject()
    def test_victim_gid_not_the_observed_endpoint(self):self.victim('ready',lambda v:v['gid'].__setitem__(0,v['gid'][0]^1));self.reject()
    def test_victim_udp_is_forbidden(self):self.victim('ready',lambda v:v['provenance']['owned_udp_sockets'].append({'inode':'fake'}));self.reject()
    def test_peer_arm_causality_missing(self):(self.root/(A+'.peer_armed.json')).write_text('{}');self.reject()
    def test_victim_terminal_missing(self):
        p=self.root/(A+'.victim.log');p.write_text(p.read_text().replace('MDDS_CLI_TERMINAL ','REMOVED '));self.reject()
    def test_victim_service_callback_missing(self):
        p=self.root/(B+'.victim.log');p.write_text(p.read_text().replace('ABRUPT_SERVICE_RX ','REMOVED '));self.reject()
    def test_parent_terminal_missing(self):
        p=self.root/(A+'.ros.log');p.write_text(p.read_text().replace('MDDS_CLI_TERMINAL ','REMOVED '));self.reject()

if __name__=='__main__':unittest.main(argv=[sys.argv[0]])
