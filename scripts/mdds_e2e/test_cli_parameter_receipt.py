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


class ParameterReceipt(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='mdds_parameter_receipt_');self.root=Path(self.temp.name)
        for p in SOURCE.iterdir():
            if p.is_file() and (p.suffix in ('.json','.log','.so','.yaml') or p.name in ('nonce','cli_batch')):shutil.copyfile(p,self.root/p.name)
        self.value=json.loads((self.root/(BOARD+'.cli.results.json')).read_text(encoding='utf-8'))
    def tearDown(self):self.temp.cleanup()
    def verify(self):validate_report(self.value,self.root,self.value['run_id'],BOARD,self.value['nonce'])
    def reject(self):
        with self.assertRaises((ValueError,OSError)):self.verify()
    def test_real(self):self.verify()
    def test_missing_array_read(self):
        self.value['results']=[r for r in self.value['results'] if r['label']!='param_get_octets'];self.reject()
    def test_wrong_type(self):
        next(r for r in self.value['results'] if r['label']=='param_get_flag')['expected']['label']='Integer value is:';self.reject()
    def test_wrong_state(self):
        p=self.root/(PEER+'.parameter_state.json');data=json.loads(p.read_text(encoding='utf-8'));data['values']['count']+=1;p.write_text(json.dumps(data),encoding='utf-8');self.reject()
    def test_dump_changed(self):
        p=self.root/(BOARD+'.parameters_dump.yaml');p.write_text(p.read_text(encoding='utf-8')+'# modified\n',encoding='utf-8');self.reject()
    def test_missing_readonly_description(self):
        self.value['results']=[r for r in self.value['results'] if r['label']!='param_describe_locked'];self.reject()


if __name__=='__main__':unittest.main()
