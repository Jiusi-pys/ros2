"""Adversarial checks against actual doctor/alias results and live reports."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
import cli_acceptance as a
from verify_cli_daemon import validate_report

SOURCE=Path(sys.argv[1]).resolve();A=a.TARGET['board_serials'][0]


class DoctorReceipt(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='doctor_receipt_');self.root=Path(self.temp.name)/SOURCE.name
        shutil.copytree(SOURCE,self.root,ignore=shutil.ignore_patterns('*.tar','*.whl','*.py','mdds_broker_daemon','mdds_token_exec'))
        self.value=json.loads((self.root/(A+'.cli.results.json')).read_text());self.nonce=(self.root/'nonce').read_text().strip()
    def tearDown(self):self.temp.cleanup()
    def check(self):validate_report(self.value,self.root,SOURCE.name,A,self.nonce)
    def reject(self):
        with self.assertRaises((ValueError,AssertionError)):self.check()
    def alter_log(self,label,old,new):
        e=next(r for r in self.value['results'] if r['label']==label)['execution'];p=self.root/(A+'.'+e['log']['path']);raw=p.read_text();self.assertIn(old,raw);p.write_text(raw.replace(old,new),newline='\n');e['log']['sha256']=a.digest(p.read_bytes())
    def test_original(self):self.check()
    def test_omitted_checks(self):self.alter_log('doctor_checks','All 5 checks passed','All 3 checks passed');self.reject()
    def test_failed_checks(self):self.alter_log('wtf_checks','All 5 checks passed','1/5 check(s) failed');self.reject()
    def test_missing_report(self):self.alter_log('doctor_report','PACKAGE VERSIONS','MISSING REPORT');self.reject()
    def test_wrong_middleware(self):self.alter_log('doctor_report','rmw_mdds','rmw_fastrtps_cpp');self.reject()
    def test_wrong_live_topic(self):self.alter_log('wtf_report','/A/alpha/out','/A/unknown/out');self.reject()
    def test_changed_reference(self):(self.root/'doctor_reference/jazzy/distribution.yaml').write_text('modified');self.reject()
    def test_wrong_dependency_version(self):
        p=self.root/(A+'.doctor_runtime.json');v=json.loads(p.read_text());v['modules']['rosdistro']['version']='0.0';p.write_text(json.dumps(v));self.reject()


if __name__=='__main__':unittest.main(argv=[sys.argv[0]])
