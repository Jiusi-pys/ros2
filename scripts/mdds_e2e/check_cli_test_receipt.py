"""Adversarial checks for actual launch-testing XML and assertion evidence."""
import json
import sys
import unittest
import xml.etree.ElementTree as ET
import cli_acceptance as a
from check_cli_process_receipt import ProcessReceipt,A


class TestReceipt(ProcessReceipt):
    def alter_xml(self,change):
        p=self.root/(A+'.process_test.junit.xml');root=ET.fromstring(p.read_text());change(root);p.write_text(ET.tostring(root,encoding='unicode'))
        self.execution()['process']['junit_sha256']=a.digest(p.read_bytes())
    def test_rehashed_hidden_failure(self):self.alter_xml(lambda root:ET.SubElement(next(root.iter('testcase')),'failure'));self.reject()
    def test_rehashed_skipped_suite(self):self.alter_xml(lambda root:root.find('testsuite').set('skipped','1'));self.reject()
    def test_rehashed_forged_total(self):self.alter_xml(lambda root:root.set('tests','99'));self.reject()
    def test_missing_executed_assertion(self):
        p=self.root/(A+'.process_test_assertions.json');v=json.loads(p.read_text());v['assertions'].remove('peer_exchange');p.write_text(json.dumps(v));self.reject()
    def test_wrong_native_exit_in_assertions(self):
        p=self.root/(A+'.process_test_assertions.json');v=json.loads(p.read_text());v['native_returncode']=-11;p.write_text(json.dumps(v));self.reject()
    def test_changed_installed_definition(self):(self.root/'peer_talker_test.py').write_text('changed');self.reject()
    def test_changed_test_configuration(self):
        p=self.root/(A+'.process_test_config.json');v=json.loads(p.read_text());v['nonce']='stale';p.write_text(json.dumps(v));self.reject()


if __name__=='__main__':unittest.main(argv=[sys.argv[0]],defaultTest='TestReceipt')
