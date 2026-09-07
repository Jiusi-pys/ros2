"""Adversarial validation of an actual cross-board recording and replay run."""
import hashlib
import json
from pathlib import Path
import shutil
import sqlite3
import sys
import tempfile
import unittest
from contextlib import closing
import yaml
from verify_bags import validate,local_file

SOURCE=Path(sys.argv[1]).resolve()
A='3e01ff55454d202020104033bf453b00'
B='3e01ff55454d202020104433991c3b00'


class BagReceipt(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='bag_receipt_')
        self.root=Path(self.temp.name)/SOURCE.name
        shutil.copytree(SOURCE,self.root,ignore=shutil.ignore_patterns('*.tar','*.py','mdds_broker_daemon','mdds_token_exec'))
        self.value=json.loads((self.root/(A+'.cli.results.json')).read_text(encoding='utf-8'))
        self.nonce=(self.root/'nonce').read_text().strip()
    def tearDown(self):self.temp.cleanup()
    def check(self):validate(self.value,self.root,SOURCE.name,A,self.nonce)
    def reject(self):
        with self.assertRaises((ValueError,AssertionError)):self.check()
    def update_hash(self,path):
        files=self.value['bag_files']
        for item in files:
            if local_file(self.root,A,item['path'])==path:
                item.update(size=path.stat().st_size,sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        (self.root/(A+'.bag_files.json')).write_text(json.dumps(files))
    def record(self):return next(r for r in self.value['results'] if r['label']=='bag_record_sqlite3')['execution']
    def test_original(self):self.check()
    def test_rehashed_wrong_cdr_payload(self):
        p=self.root/(A+'.bags_sqlite3_sqlite3_0.db3')
        with closing(sqlite3.connect(p)) as db, db:
            row=db.execute('SELECT id,data FROM messages LIMIT 1').fetchone()
            raw=bytearray(row[1]);raw[8]=ord('X')
            db.execute('UPDATE messages SET data=? WHERE id=?',(bytes(raw),row[0]))
        self.update_hash(p);self.reject()
    def test_rehashed_zero_storage_time(self):
        p=self.root/(A+'.bags_sqlite3_sqlite3_0.db3')
        with closing(sqlite3.connect(p)) as db, db:db.execute('UPDATE messages SET timestamp=0')
        self.update_hash(p);self.reject()
    def test_rehashed_wrong_metadata(self):
        p=self.root/(A+'.bags_sqlite3_metadata.yaml');v=yaml.safe_load(p.read_text())
        v['rosbag2_bagfile_information']['duration']['nanoseconds']+=1000000
        p.write_text(yaml.safe_dump(v));self.update_hash(p);self.reject()
    def test_missing_cross_board_replay(self):
        p=self.root/(B+'.bag_sqlite3_played.json');v=json.loads(p.read_text());v['received'].pop();p.write_text(json.dumps(v));self.reject()
    def test_fabricated_native_read(self):
        p=self.root/(A+'.bag_sqlite3_inspection.json');v=json.loads(p.read_text());v['records'][0]['timestamp']+=1;p.write_text(json.dumps(v));self.reject()
    def test_owned_udp(self):self.record()['bag_record']['native']['owned_udp']=['socket'];self.reject()
    def test_foreign_recorder_binary(self):
        hashes=self.record()['bag_record']['native']['hashes'];hashes['/tmp/librmw_mdds.so']=hashes.pop(next(p for p in hashes if p.endswith('/librmw_mdds.so')));self.reject()
    def test_emergency_stop(self):self.record()['bag_record']['emergency_cleanup']=True;self.reject()


if __name__=='__main__':unittest.main(argv=[sys.argv[0]])
