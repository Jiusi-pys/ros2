"""Reject absent controls, leaking OFF entities, and fabricated isolation."""
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

class OffReceipt(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)/SOURCE.name
        shutil.copytree(SOURCE,self.root,ignore=shutil.ignore_patterns('*.tar','*.py','mdds_broker_daemon','mdds_token_exec'))
        self.reports={b:json.loads((self.root/(b+'.cli.results.json')).read_bytes()) for b in (A,B)};self.nonce=(self.root/'nonce').read_text().strip()
    def tearDown(self):self.temp.cleanup()
    def check(self):
        for b,v in self.reports.items():validate_report(v,self.root,SOURCE.name,b,self.nonce)
        manifest=json.loads((self.root/'cli_acceptance_manifest.json').read_bytes());manifest['run_id']=SOURCE.name
        if emit(self.root,manifest,self.reports,SOURCE.name,self.nonce)!='graph:discovery_off':raise ValueError('OFF case not accepted')
    def reject(self):
        with self.assertRaises((ValueError,KeyError,FileNotFoundError)):self.check()
    def change(self,operation):
        v=self.reports[A]['discovery_off'];old=json.dumps(v);operation(v)
        (self.root/(A+'.discovery_off.json')).write_text(json.dumps(v))
        p=self.root/(A+'.ros.log');p.write_text(p.read_text().replace(old,json.dumps(v)))
    def test_original(self):self.check()
    def test_off_remote_node_leak(self):self.change(lambda v:v['snapshots']['off']['nodes'].append(['on_B','/foreign','/']));self.reject()
    def test_off_matching_leak(self):self.change(lambda v:v['counts'].update(off_writer_matches=2));self.reject()
    def test_service_boundary_leak(self):self.change(lambda v:v['ready'].update(off_on_local=True));self.reject()
    def test_off_received_foreign_data(self):self.change(lambda v:v['received']['off'].append(v['received']['on'][0]));self.reject()
    def test_local_off_data_must_work(self):self.change(lambda v:v['received'].update(off=[]));self.reject()
    def test_enabled_remote_control_required(self):self.change(lambda v:v['received'].update(on=[]));self.reject()
    def test_actual_negative_send_required(self):self.change(lambda v:v['sent'].update(off=[]));self.reject()
    def test_observation_window_required(self):self.change(lambda v:v.update(observation_ns=0));self.reject()
    def test_context_setting_required(self):self.change(lambda v:v['settings'][0].update(range='SYSTEM_DEFAULT'));self.reject()
    def test_native_receiving_callback_required(self):
        p=self.root/(B+'.ros.log');p.write_text(p.read_text().replace('DISCOVERY_OFF_RX ','REMOVED '));self.reject()
    def test_native_case_exit_required(self):
        p=self.root/(A+'.ros.log');p.write_text(p.read_text().replace('MDDS_CLI_TERMINAL ','REMOVED '));self.reject()
    def test_both_senders_barrier_required(self):(self.root/(B+'.discovery_off.sent')).write_text('old');self.reject()

if __name__=='__main__':unittest.main(argv=[sys.argv[0]])
