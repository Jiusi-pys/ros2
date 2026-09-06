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


class ParameterChangeReceipt(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='mdds_parameter_change_');self.root=Path(self.temp.name)
        for p in SOURCE.iterdir():
            if p.is_file() and (p.suffix in ('.json','.log','.so','.yaml') or p.name in ('nonce','cli_batch')):shutil.copyfile(p,self.root/p.name)
        self.value=json.loads((self.root/(BOARD+'.cli.results.json')).read_text(encoding='utf-8'))
    def tearDown(self):self.temp.cleanup()
    def verify(self):validate_report(self.value,self.root,self.value['run_id'],BOARD,self.value['nonce'])
    def reject(self):
        with self.assertRaises((ValueError,OSError)):self.verify()
    def mutate(self,name,change):
        p=self.root/name;v=json.loads(p.read_text(encoding='utf-8'));change(v);p.write_text(json.dumps(v),encoding='utf-8')
    def test_real(self):self.verify()
    def test_missing_restore(self):
        self.value['results']=[r for r in self.value['results'] if r['label']!='param_restore_readback'];self.reject()
    def test_missing_loaded_array(self):
        self.value['results']=[r for r in self.value['results'] if r['label']!='param_loaded_flags'];self.reject()
    def test_missing_event(self):
        self.mutate(BOARD+'.parameter_events.json',lambda v:v['events'].pop(1));self.reject()
    def test_wrong_event_node(self):
        self.mutate(BOARD+'.parameter_events.json',lambda v:v['events'][0].update(node='/foreign'));self.reject()
    def test_still_declared(self):
        self.mutate(PEER+'.parameter_final.json',lambda v:v['values'].update(ephemeral='delete_me'));self.reject()
    def test_unloaded_value(self):
        self.mutate(PEER+'.parameter_final.json',lambda v:v['values'].update(ratio=1.25));self.reject()
    def test_changed_load_file(self):
        (self.root/(BOARD+'.parameter_load.yaml')).write_text('{}\n',encoding='utf-8');self.reject()
    def test_missing_event_log(self):
        p=self.root/(BOARD+'.ros.log');p.write_text('\n'.join(line for line in p.read_text(encoding='utf-8').splitlines() if not line.startswith('CLI_PARAMETER_EVENT ')),encoding='utf-8');self.reject()


if __name__=='__main__':unittest.main()
