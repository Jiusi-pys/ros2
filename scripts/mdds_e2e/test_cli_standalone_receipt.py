import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from verify_cli_daemon import validate_report

SOURCE=Path(sys.argv.pop(1)).resolve()
BOARD='3e01ff55454d202020104033bf453b00'
PEER='3e01ff55454d202020104433991c3b00'


class StandaloneReceipt(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='mdds_standalone_receipt_');self.root=Path(self.temp.name)
        for p in SOURCE.iterdir():
            if p.is_file() and (p.suffix in ('.json','.log','.so') or '.standalone.' in p.name or p.name in ('nonce','cli_batch','component_container')):shutil.copyfile(p,self.root/p.name)
        self.value=json.loads((self.root/(BOARD+'.cli.results.json')).read_text(encoding='utf-8'))
        self.result=next(r for r in self.value['results'] if r['case_id']=='cli:component/standalone')
    def tearDown(self):self.temp.cleanup()
    def verify(self):validate_report(self.value,self.root,self.value['run_id'],BOARD,self.value['nonce'])
    def reject(self):
        with self.assertRaises((ValueError,OSError)):self.verify()
    def change(self,name,mutate):
        p=self.root/name;value=json.loads(p.read_text(encoding='utf-8'));mutate(value);p.write_text(json.dumps(value),encoding='utf-8')
    def test_real(self):self.verify()
    def test_foreign_native_parent(self):self.result['execution']['standalone']['native']['parent_pid']=99;self.reject()
    def test_wrong_library(self):self.result['execution']['standalone']['native']['hashes']={};self.reject()
    def test_not_gone(self):self.result['execution']['standalone']['container_gone']=False;self.reject()
    def test_emergency_kill(self):self.result['execution']['standalone']['emergency_cleanup']=True;self.reject()
    def test_wrong_barrier(self):self.result['execution']['standalone']['shutdown']['barrier_nonce']='0'*32;self.reject()
    def test_missing_payload(self):self.change(PEER+'.standalone_received.json',lambda v:v.update(received=[]));self.reject()
    def test_wrong_endpoint(self):self.change(PEER+'.standalone_received.json',lambda v:v['endpoint'].update(node='foreign'));self.reject()
    def test_graph_remains(self):self.change(PEER+'.standalone_gone.json',lambda v:v.update(publisher_absent=False));self.reject()
    def test_missing_peer_ready(self):(self.root/(PEER+'.standalone.ready')).unlink();self.reject()
    def test_wrong_start_barrier(self):(self.root/(BOARD+'.standalone.start')).write_text('other\n',encoding='utf-8');self.reject()
    def test_wrong_signal_target(self):self.result['execution']['standalone']['shutdown']['cli_pid']+=1;self.reject()


if __name__=='__main__':unittest.main()
