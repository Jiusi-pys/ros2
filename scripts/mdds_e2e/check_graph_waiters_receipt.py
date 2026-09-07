"""Adversaries for the real two-observer rclcpp graph notification probe."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
import cli_acceptance as a
from verify_cli_daemon import validate_report

SOURCE=Path(sys.argv[1]).resolve();A,B=a.TARGET['board_serials']


class GraphWaiterReceipt(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='graph_waiters_receipt_');self.root=Path(self.temp.name)/SOURCE.name
        shutil.copytree(SOURCE,self.root,ignore=shutil.ignore_patterns('*.tar','*.py','mdds_broker_daemon','mdds_token_exec'))
        self.value=json.loads((self.root/(A+'.cli.results.json')).read_bytes());self.nonce=(self.root/'nonce').read_text().strip()
    def tearDown(self):self.temp.cleanup()
    def check(self):validate_report(self.value,self.root,SOURCE.name,A,self.nonce)
    def reject(self):
        with self.assertRaises(ValueError):self.check()
    def change_output(self,before,after):
        p=self.root/(A+'.graph_waiters.stdout');raw=p.read_text();self.assertIn(before,raw);p.write_text(raw.replace(before,after));self.reject()
    def test_original(self):self.check()
    def test_second_waiter_must_wake(self):self.change_output('"second":true','"second":false')
    def test_snapshot_must_converge(self):self.change_output('"snapshot":true','"snapshot":false')
    def test_context_must_be_shared(self):self.change_output('GRAPH_WAITER_CONTEXT_SHARED true','GRAPH_WAITER_CONTEXT_SHARED false')
    def test_all_entity_kinds_required(self):self.change_output('"phase":"client_destroy"','"phase":"client_create"')
    def test_native_failure(self):self.value['graph_waiters']['returncode']=1;self.reject()
    def test_udp_is_rejected(self):self.value['graph_waiters']['native']['owned_udp']=['socket'];self.reject()
    def test_foreign_executable(self):self.value['graph_waiters']['native']['executable']='/tmp/foreign';self.reject()
    def test_stale_start_barrier(self):(self.root/(A+'.graph_waiters.start')).write_text('old');self.reject()
    def test_modified_source(self):(self.root/'graph_waiters.cpp').write_text('changed');self.reject()
    def test_peer_response_callback_required(self):
        p=self.root/(B+'.graph_waiters_server.json');v=json.loads(p.read_bytes());v['sum']+=1;p.write_text(json.dumps(v));self.reject()


if __name__=='__main__':unittest.main(argv=[sys.argv[0]])
