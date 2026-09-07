"""Adversarial checks for policy generation from live graph ownership."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
import cli_acceptance as a
from verify_cli_daemon import validate_report

SOURCE=Path(sys.argv[1]).resolve();A=a.TARGET['board_serials'][0]


class PolicyReceipt(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='policy_receipt_');self.root=Path(self.temp.name)/SOURCE.name
        shutil.copytree(SOURCE,self.root,ignore=shutil.ignore_patterns('*.tar','*.py','mdds_broker_daemon','mdds_token_exec'))
        self.value=json.loads((self.root/(A+'.cli.results.json')).read_text());self.nonce=(self.root/'nonce').read_text().strip()
    def tearDown(self):self.temp.cleanup()
    def check(self):validate_report(self.value,self.root,SOURCE.name,A,self.nonce)
    def reject(self):
        with self.assertRaises((ValueError,AssertionError)):self.check()
    def alter_policy(self,change):
        e=next(r for r in self.value['results'] if r['label']=='policy_cached')['execution'];p=self.root/(A+'.'+e['policy_file']['path']);tree=ET.fromstring(p.read_text());change(tree);p.write_text(ET.tostring(tree,encoding='unicode'));e['policy_file']['sha256']=a.digest(p.read_bytes())
    def test_original(self):self.check()
    def test_rehashed_wrong_enclave(self):self.alter_policy(lambda t:t.find('.//enclave').set('path','/wrong'));self.reject()
    def test_rehashed_missing_permission(self):
        def change(t):
            group=t.find('.//topics');group.remove(group[0])
        self.alter_policy(change);self.reject()
    def test_rehashed_wrong_direction(self):
        def change(t):
            group=t.find('.//topics');group.attrib.clear();group.set('invalid_direction','ALLOW')
        self.alter_policy(change);self.reject()
    def test_rehashed_wildcard(self):self.alter_policy(lambda t:setattr(t.find('.//topic'),'text','*'));self.reject()
    def test_rehashed_missing_action_lane(self):
        def change(t):
            for group in t.iter('services'):
                for service in list(group):
                    if service.text.endswith('/_action/send_goal'):group.remove(service);return
            raise AssertionError('no action service found')
        self.alter_policy(change);self.reject()
    def test_direct_query_failure_cannot_be_hidden(self):next(r for r in self.value['results'] if r['label']=='policy_direct')['execution']['returncode']=1;self.reject()


if __name__=='__main__':unittest.main(argv=[sys.argv[0]])
