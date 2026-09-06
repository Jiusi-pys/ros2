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


class ComponentReceipt(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='mdds_component_receipt_');self.root=Path(self.temp.name)
        for p in SOURCE.iterdir():
            if p.is_file() and (p.suffix in ('.json','.log','.so','.pid') or p.name in ('nonce','cli_batch','component_container')):shutil.copyfile(p,self.root/p.name)
        self.value=json.loads((self.root/(BOARD+'.cli.results.json')).read_text(encoding='utf-8'))
    def tearDown(self):self.temp.cleanup()
    def verify(self):validate_report(self.value,self.root,self.value['run_id'],BOARD,self.value['nonce'])
    def reject(self):
        with self.assertRaises((ValueError,OSError)):self.verify()
    def change(self,name,mutate):
        p=self.root/name;value=json.loads(p.read_text(encoding='utf-8'));mutate(value);p.write_text(json.dumps(value),encoding='utf-8')
    def test_real(self):self.verify()
    def test_wrong_container_library(self):self.value['container']['hashes']={};self.reject()
    def test_udp(self):self.value['container']['owned_udp']=['socket'];self.reject()
    def test_wrong_child(self):self.value['container']['pid']+=1;self.reject()
    def test_missing_survivor_list(self):
        self.value['results']=[r for r in self.value['results'] if r['label']!='component_list_survivor'];self.reject()
    def test_wrong_endpoint_owner(self):self.change(BOARD+'.components_loaded.json',lambda v:v['primary'].update(node='wrong'));self.reject()
    def test_survivor_recreated(self):self.change(BOARD+'.components_retired.json',lambda v:v['survivor'].update(gid=[0]*16));self.reject()
    def test_missing_payload(self):self.change(BOARD+'.components_loaded.json',lambda v:v['received'].update(primary=[]));self.reject()
    def test_container_failed_exit(self):self.change(BOARD+'.container.status.json',lambda v:v.update(returncode=1));self.reject()


if __name__=='__main__':unittest.main()
