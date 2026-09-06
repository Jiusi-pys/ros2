"""Validate goal UUID, payload and lifecycle evidence using an actual batch."""
import copy
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


class ActionReceipt(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='mdds_action_receipt_');self.root=Path(self.temp.name)
        for p in SOURCE.iterdir():
            if p.is_file() and (p.suffix in ('.json','.log','.so') or p.name in ('nonce','cli_batch')):shutil.copyfile(p,self.root/p.name)
        self.value=json.loads((self.root/(BOARD+'.cli.results.json')).read_text(encoding='utf-8'))
    def tearDown(self):self.temp.cleanup()
    def verify(self):validate_report(self.value,self.root,self.value['run_id'],BOARD,self.value['nonce'])
    def change_peer(self,key,value):
        path=self.root/(PEER+'.action_goal.json');record=json.loads(path.read_text(encoding='utf-8'));record[key]=value
        path.write_text(json.dumps(record),encoding='utf-8')
    def reject(self):
        with self.assertRaises((ValueError,OSError)):self.verify()
    def test_real(self):self.verify()
    def test_wrong_uuid(self):self.change_peer('goal_id','00'*16);self.reject()
    def test_wrong_order(self):self.change_peer('order',6);self.reject()
    def test_wrong_feedback(self):self.change_peer('feedback',[]);self.reject()
    def test_wrong_result(self):self.change_peer('result',[0,1]);self.reject()
    def test_wrong_board(self):self.change_peer('board',BOARD);self.reject()
    def test_repeated_goal(self):self.change_peer('count',2);self.reject()
    def test_missing_callback(self):
        path=self.root/(PEER+'.ros.log');path.write_text('\n'.join(line for line in path.read_text(encoding='utf-8').splitlines() if not line.startswith('CLI_ACTION_GOAL ')),encoding='utf-8');self.reject()
    def test_missing_count_variant(self):
        self.value['results']=[r for r in self.value['results'] if r['label']!='action_info_count'];self.reject()


if __name__=='__main__':unittest.main()
