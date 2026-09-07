"""Reject absent second-domain controls, foreign data, and broker relabeling."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from cli_acceptance import TARGET
from verify_cli_daemon import validate_report
from service_graph_receipt import emit
SOURCE=Path(sys.argv[1]).resolve();A,B=TARGET['board_serials']

class DomainReceipt(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)/SOURCE.name
        shutil.copytree(SOURCE,self.root,ignore=shutil.ignore_patterns('*.tar','*.py','mdds_token_exec'))
        self.reports={b:json.loads((self.root/(b+'.cli.results.json')).read_bytes()) for b in (A,B)};self.nonce=(self.root/'nonce').read_text().strip()
    def tearDown(self):self.temp.cleanup()
    def check(self):
        for b,v in self.reports.items():validate_report(v,self.root,SOURCE.name,b,self.nonce)
        manifest=json.loads((self.root/'cli_acceptance_manifest.json').read_bytes());manifest['run_id']=SOURCE.name
        if emit(self.root,manifest,self.reports,SOURCE.name,self.nonce)!='graph:domain_isolation':raise ValueError('domain case missing')
    def reject(self):
        with self.assertRaises((ValueError,KeyError,FileNotFoundError)):self.check()
    def change(self,operation):
        value=self.reports[A]['domain_isolation'];old=json.dumps(value);operation(value)
        (self.root/(A+'.domain_isolation.json')).write_text(json.dumps(value))
        p=self.root/(A+'.ros.log');p.write_text(p.read_text().replace(old,json.dumps(value)))
    def test_original(self):self.check()
    def test_actual_domain_required(self):self.change(lambda v:v['domains']['176'].update(actual_domain=175));self.reject()
    def test_foreign_sample_rejected(self):self.change(lambda v:v['domains']['175']['received'].append(v['domains']['176']['received'][0]));self.reject()
    def test_both_positive_controls_required(self):self.change(lambda v:v['domains']['176'].update(received=[]));self.reject()
    def test_absent_peers_are_not_isolation(self):self.change(lambda v:v['domains']['175']['counts'].update(writer_matches=0));self.reject()
    def test_foreign_endpoint_count_rejected(self):self.change(lambda v:v['domains']['175']['counts'].update(publishers=4));self.reject()
    def test_node_domain_enclave_must_match(self):self.change(lambda v:v['domains']['176']['snapshot']['nodes'][0].__setitem__(2,'/isolation/d175/A'));self.reject()
    def test_both_domains_must_publish(self):self.change(lambda v:v['domains']['176'].update(sent=[]));self.reject()
    def test_observation_window_required(self):self.change(lambda v:v.update(observation_ns=0));self.reject()
    def test_secondary_broker_must_be_real_domain_176(self):
        p=self.root/(A+'.domain_daemon.inspect.json');v=json.loads(p.read_bytes());v['argv'][v['argv'].index('--domain')+1]='175';p.write_text(json.dumps(v));self.reject()
    def test_secondary_broker_udp_rejected(self):
        p=self.root/(B+'.domain_daemon.inspect.json');v=json.loads(p.read_bytes());v['owned_udp']=['unexpected'];p.write_text(json.dumps(v));self.reject()
    def test_secondary_broker_must_exit_cleanly(self):
        p=self.root/(B+'.domain_daemon.status.json');v=json.loads(p.read_bytes());v['returncode']=1;p.write_text(json.dumps(v));self.reject()
    def test_receiving_callback_required(self):
        p=self.root/(B+'.ros.log');p.write_text(p.read_text().replace('DOMAIN_RX ','REMOVED '));self.reject()
    def test_native_case_marker_required(self):
        p=self.root/(A+'.ros.log');p.write_text(p.read_text().replace('MDDS_CLI_TERMINAL ','REMOVED '));self.reject()

if __name__=='__main__':unittest.main(argv=[sys.argv[0]])
