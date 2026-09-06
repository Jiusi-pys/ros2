"""Reject corrupted introspection identity/content in completed physical evidence."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
import cli_acceptance as acceptance
from verify_cli_daemon import validate_report

SOURCE=Path(sys.argv.pop(1)).resolve()
BOARD='3e01ff55454d202020104033bf453b00'
PEER='3e01ff55454d202020104433991c3b00'


class EchoReceipt(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='mdds_echo_receipt_');self.root=Path(self.temp.name)
        for p in SOURCE.iterdir():
            if p.is_file() and (p.suffix in ('.json','.log','.so') or p.name in ('nonce','cli_batch')):shutil.copyfile(p,self.root/p.name)
        self.value=json.loads((self.root/(BOARD+'.cli.results.json')).read_text(encoding='utf-8'))
    def tearDown(self):self.temp.cleanup()
    def verify(self):validate_report(self.value,self.root,self.value['run_id'],BOARD,self.value['nonce'])
    def change(self,board,kind,key,value):
        path=self.root/(board+'.introspection.'+kind+'.json');record=json.loads(path.read_text(encoding='utf-8'));record[key]=value;path.write_text(json.dumps(record),encoding='utf-8')
    def reject(self):
        with self.assertRaises((ValueError,OSError)):self.verify()
    def test_real(self):self.verify()
    def test_wrong_gid(self):self.change(BOARD,'result','client_gid',[0]*16);self.reject()
    def test_wrong_sequence(self):self.change(BOARD,'result','sequence_number',9999);self.reject()
    def test_wrong_request(self):self.change(BOARD,'result','a',0);self.reject()
    def test_wrong_server_result(self):self.change(PEER,'server','sum',0);self.reject()
    def test_missing_client_record(self):(self.root/(BOARD+'.introspection.result.json')).unlink();self.reject()
    def test_wrong_server_board(self):self.change(PEER,'server','board',BOARD);self.reject()
    def test_missing_event_even_with_new_hash(self):
        result=next(r for r in self.value['results'] if r['case_id']=='cli:service/echo');ref=result['execution']['log'];path=self.root/(BOARD+'.'+ref['path'])
        text=path.read_text(encoding='utf-8');self.assertIn('REQUEST_SENT',text)
        path.write_text(text.replace('REQUEST_SENT','OTHER_EVENT'),encoding='utf-8',newline='\n');ref['sha256']=acceptance.digest(path.read_bytes());self.reject()
    def test_timeout_exit(self):
        next(r for r in self.value['results'] if r['case_id']=='cli:service/echo')['execution']['returncode']=124;self.reject()


if __name__=='__main__':unittest.main()
