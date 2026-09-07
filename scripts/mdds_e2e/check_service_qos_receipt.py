"""Reject false service readiness and unbound peer responses in board evidence."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
import cli_acceptance as a
from verify_cli_daemon import validate_report

SOURCE=Path(sys.argv[1]).resolve();A,B=a.TARGET['board_serials']


class ServiceQoSReceipt(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='service_qos_receipt_');self.root=Path(self.temp.name)/SOURCE.name
        shutil.copytree(SOURCE,self.root,ignore=shutil.ignore_patterns('*.tar','*.py','mdds_broker_daemon','mdds_token_exec'))
        self.value=json.loads((self.root/(A+'.cli.results.json')).read_bytes());self.nonce=(self.root/'nonce').read_text().strip()
    def tearDown(self):self.temp.cleanup()
    def check(self):validate_report(self.value,self.root,SOURCE.name,A,self.nonce)
    def reject(self):
        with self.assertRaises(ValueError):self.check()
    def test_original(self):self.check()
    def test_response_incompatible(self):self.value['service_qos']['ready_checks'][0]['bad_response']=True;self.reject()
    def test_request_incompatible(self):self.value['service_qos']['ready_checks'][0]['bad_request']=True;self.reject()
    def test_no_service_is_not_a_qos_negative_control(self):next(iter(self.value['service_qos']['counts'].values()))['servers']=0;self.reject()
    def test_wrong_client_count(self):next(iter(self.value['service_qos']['counts'].values()))['clients']=1;self.reject()
    def test_wrong_sibling_ownership(self):self.value['service_qos']['nonowners']['duplicate_B']['services']=self.value['service_qos']['services'];self.reject()
    def test_positive_control_must_succeed(self):self.value['service_qos']['ready_checks'][2]['good_reliable']=False;self.reject()
    def test_exact_response_required(self):self.value['service_qos']['calls'][0]['sum']=0;self.reject()
    def test_peer_callback_required(self):
        p=self.root/(B+'.service_qos_server.json');v=json.loads(p.read_bytes());v['served'].pop();p.write_text(json.dumps(v));self.reject()
    def test_raw_peer_callback_required(self):
        p=self.root/(B+'.ros.log');p.write_text(p.read_text().replace('SERVICE_QOS_SERVER ','REMOVED '));self.reject()


if __name__=='__main__':unittest.main(argv=[sys.argv[0]])
