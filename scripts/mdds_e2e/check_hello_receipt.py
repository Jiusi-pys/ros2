"""Adversarial checks for real hello ROS/UDP lane separation and lifecycle."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
import cli_acceptance as a
from verify_cli_daemon import validate_report

SOURCE=Path(sys.argv[1]).resolve();A=a.TARGET['board_serials'][0]


class HelloReceipt(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='hello_receipt_');self.root=Path(self.temp.name)/SOURCE.name
        shutil.copytree(SOURCE,self.root,ignore=shutil.ignore_patterns('*.tar','*.whl','*.py','mdds_broker_daemon','mdds_token_exec'))
        self.value=json.loads((self.root/(A+'.cli.results.json')).read_text());self.nonce=(self.root/'nonce').read_text().strip()
    def tearDown(self):self.temp.cleanup()
    def check(self):validate_report(self.value,self.root,SOURCE.name,A,self.nonce)
    def reject(self):
        with self.assertRaises((ValueError,AssertionError)):self.check()
    def execution(self):return next(r for r in self.value['results'] if r['label']=='hello_doctor')['execution']
    def test_original(self):self.check()
    def test_missing_ros_callbacks(self):
        p=self.root/(A+'.hello_doctor_received.json');v=json.loads(p.read_text());v['received']=[];p.write_text(json.dumps(v));self.reject()
    def test_foreign_binary(self):self.execution()['hello_process']['native']['hashes']['/tmp/libmdds.so']='0'*64;self.reject()
    def test_extra_udp_socket(self):self.execution()['hello_process']['native']['diagnostic_udp'].append({'table':'udp','local':'00000000:8888','remote':'00000000:0000'});self.reject()
    def test_wrong_udp_port(self):
        self.execution()['hello_process']['native']['diagnostic_udp']=[{'table':'udp','local':'00000000:8888','remote':'00000000:0000'}]*2;self.reject()
    def test_emergency_cleanup(self):self.execution()['hello_process']['emergency_cleanup']=True;self.reject()
    def test_stale_barrier(self):(self.root/(A+'.hello_doctor.stop')).write_text('stale');self.reject()
    def test_fabricated_observation(self):self.execution()['hello_process']['observed_stdout']='fabricated';self.reject()


if __name__=='__main__':unittest.main(argv=[sys.argv[0]])
