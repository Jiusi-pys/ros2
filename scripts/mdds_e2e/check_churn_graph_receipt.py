"""Reject missing churn phases, stale ownership and fabricated progress."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
import cli_acceptance as a
from service_graph_receipt import emit
from verify_cli_daemon import validate_report
from churn_graph_contract import scope

SOURCE=Path(sys.argv[1]).resolve();A,B=a.TARGET['board_serials']

class ChurnReceipt(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='churn_receipt_');self.root=Path(self.temp.name)/SOURCE.name
        shutil.copytree(SOURCE,self.root,ignore=shutil.ignore_patterns('*.tar','*.py','mdds_broker_daemon','mdds_token_exec'))
        self.reports={b:json.loads((self.root/(b+'.cli.results.json')).read_bytes()) for b in (A,B)}
        self.nonce=(self.root/'nonce').read_text().strip()
    def tearDown(self):self.temp.cleanup()
    def check(self):
        for b,v in self.reports.items():validate_report(v,self.root,SOURCE.name,b,self.nonce)
        p=self.root/'cli_partial_manifest.json'
        if not p.exists():p=self.root/'cli_acceptance_manifest.json'
        manifest=json.loads(p.read_bytes())
        if emit(self.root,manifest,self.reports,SOURCE.name,self.nonce)!='graph:churn':raise ValueError('churn case not issued')
    def reject(self):
        with self.assertRaises((ValueError,KeyError,FileNotFoundError)):self.check()
    def mutate(self,board,operation):
        report=self.reports[board]['churn_graph'];before=json.dumps(report)
        old_phases=[json.dumps(v) for v in report['phases']]
        operation(report)
        (self.root/(board+'.churn_graph.json')).write_text(json.dumps(report))
        p=self.root/(board+'.ros.log');raw=p.read_text().replace(before,json.dumps(report))
        for old,new in zip(old_phases,report['phases']):raw=raw.replace('CHURN_PHASE '+old,'CHURN_PHASE '+json.dumps(new))
        p.write_text(raw)
    def test_original(self):self.check()
    def test_phase_omitted(self):self.mutate(A,lambda v:v['phases'].pop());self.reject()
    def test_survivor_replaced(self):self.mutate(A,lambda v:v['phases'][17]['snapshot']['gids']['A:survivor'].__setitem__(3,v['phases'][17]['snapshot']['gids']['A:survivor'][3]^1));self.reject()
    def test_transient_identity_reused(self):self.mutate(A,lambda v:v['phases'][2]['snapshot']['gids'].__setitem__('A:transient',v['phases'][0]['snapshot']['gids']['A:transient']));self.reject()
    def test_removed_parameter_publisher_ghost(self):self.mutate(A,lambda v:v['phases'][1]['snapshot']['parameter_event_owners'].append(scope(SOURCE.name)+'/A/transient'));self.reject()
    def test_final_node_ghost(self):self.mutate(A,lambda v:v['final']['nodes'].append(scope(SOURCE.name)+'/A/transient'));self.reject()
    def test_missing_action_client(self):self.mutate(A,lambda v:v['phases'][0]['snapshot']['by_node'][scope(SOURCE.name)+'/A/transient']['action_clients'].clear());self.reject()
    def test_peer_phase_not_observed(self):self.mutate(A,lambda v:v['advances'][7].update(peer_ready=6));self.reject()
    def test_query_proof_missing(self):self.mutate(A,lambda v:v.update(query_attempts=0));self.reject()
    def test_survivor_data_missing(self):self.mutate(B,lambda v:v['phases'][39]['data'].clear());self.reject()
    def test_control_callback_missing(self):
        p=self.root/(A+'.ros.log');p.write_text(p.read_text().replace('CHURN_CONTROL_RX ','REMOVED '));self.reject()
    def test_service_callback_missing(self):
        p=self.root/(B+'.ros.log');p.write_text(p.read_text().replace('CHURN_SERVICE_RX ','REMOVED '));self.reject()
    def test_process_crash(self):
        p=self.root/(B+'.ros.status.json');v=json.loads(p.read_bytes());v['returncode']=139;p.write_text(json.dumps(v));self.reject()
    def test_terminal_missing(self):
        p=self.root/(A+'.ros.log');p.write_text(p.read_text().replace('MDDS_CLI_TERMINAL ','REMOVED '));self.reject()
    def test_case_not_frozen(self):
        p=self.root/('inputs_'+A+'.sha256');p.write_text('\n'.join(v for v in p.read_text().splitlines() if not v.endswith('  graph_case'))+'\n');self.reject()

if __name__=='__main__':unittest.main(argv=[sys.argv[0]])
