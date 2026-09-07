"""Adversaries for standalone cross-board multicast CLI receipts."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
import cli_acceptance as a
from verify_cli_daemon import validate_report

SOURCE=Path(sys.argv[1]).resolve();A=a.TARGET['board_serials'][0]


class MulticastReceipt(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='multicast_receipt_');self.root=Path(self.temp.name)/SOURCE.name
        shutil.copytree(SOURCE,self.root,ignore=shutil.ignore_patterns('*.tar','*.py','mdds_broker_daemon','mdds_token_exec'))
        self.value=json.loads((self.root/(A+'.cli.results.json')).read_text());self.nonce=(self.root/'nonce').read_text().strip()
    def tearDown(self):self.temp.cleanup()
    def check(self):validate_report(self.value,self.root,SOURCE.name,A,self.nonce)
    def reject(self):
        with self.assertRaises((ValueError,AssertionError)):self.check()
    def receiver(self):return next(r for r in self.value['results'] if r['label']=='multicast_receive')['execution']
    def test_original(self):self.check()
    def test_stale_barrier(self):(self.root/(A+'.multicast.send')).write_text('stale');self.reject()
    def test_wrong_group(self):self.receiver()['multicast_receiver']['ready']['group']='239.0.0.1';self.reject()
    def test_wrong_bound_socket(self):self.receiver()['multicast_receiver']['ready']['udp_local']='00000000:0000';self.reject()
    def test_emergency_cleanup(self):self.receiver()['multicast_receiver']['emergency_cleanup']=True;self.reject()
    def test_sender_failure(self):next(r for r in self.value['results'] if r['label']=='multicast_send')['execution']['returncode']=1;self.reject()
    def test_rehashed_wrong_payload(self):
        e=self.receiver();p=self.root/(A+'.'+e['log']['path']);raw=p.read_text();self.assertIn('Hello World!',raw);p.write_text(raw.replace('Hello World!','wrong'),newline='\n');e['log']['sha256']=a.digest(p.read_bytes());self.reject()


if __name__=='__main__':unittest.main(argv=[sys.argv[0]])
