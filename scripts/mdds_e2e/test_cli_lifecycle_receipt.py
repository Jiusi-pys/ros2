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


class LifecycleReceipt(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='mdds_lifecycle_receipt_');self.root=Path(self.temp.name)
        for p in SOURCE.iterdir():
            if p.is_file() and (p.suffix in ('.json','.log','.so') or p.name in ('nonce','cli_batch')):shutil.copyfile(p,self.root/p.name)
        self.value=json.loads((self.root/(BOARD+'.cli.results.json')).read_text(encoding='utf-8'))
    def tearDown(self):self.temp.cleanup()
    def verify(self):validate_report(self.value,self.root,self.value['run_id'],BOARD,self.value['nonce'])
    def reject(self):
        with self.assertRaises((ValueError,OSError)):self.verify()
    def change(self,name,mutate):
        p=self.root/name;value=json.loads(p.read_text(encoding='utf-8'));mutate(value);p.write_text(json.dumps(value),encoding='utf-8')
    def test_real(self):self.verify()
    def test_missing_state_readback(self):
        self.value['results']=[r for r in self.value['results'] if r['label']!='lifecycle_after_activate'];self.reject()
    def test_wrong_final(self):self.change(PEER+'.lifecycle_final.json',lambda v:v.update(state={'id':3,'label':'active'}));self.reject()
    def test_missing_callback(self):self.change(PEER+'.lifecycle_callbacks.json',lambda v:v['callbacks'].pop());self.reject()
    def test_wrong_previous_state(self):self.change(PEER+'.lifecycle_callbacks.json',lambda v:v['callbacks'][0].update(previous={'id':3,'label':'active'}));self.reject()
    def test_missing_event(self):self.change(BOARD+'.lifecycle_events.json',lambda v:v['events'].pop());self.reject()
    def test_wrong_event_state(self):self.change(BOARD+'.lifecycle_events.json',lambda v:v['events'][0].update(goal={'id':4,'label':'finalized'}));self.reject()
    def test_wrong_node(self):self.change(BOARD+'.lifecycle_events.json',lambda v:v.update(node='/other'));self.reject()
    def test_missing_raw_event(self):
        p=self.root/(BOARD+'.ros.log');p.write_text('\n'.join(line for line in p.read_text(encoding='utf-8').splitlines() if not line.startswith('CLI_LIFECYCLE_EVENT ')),encoding='utf-8');self.reject()


if __name__=='__main__':unittest.main()
