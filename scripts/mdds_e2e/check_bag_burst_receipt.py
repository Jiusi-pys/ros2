"""Adversarial checks for the real burst player's receiver and stop evidence."""
import json
import sys
import unittest
import cli_acceptance as a
from check_bag_receipt import BagReceipt,A,B,SOURCE
from verify_bag_burst import validate


class BurstReceipt(BagReceipt):
    def check(self):
        super().check()
        validate(self.value,self.root,SOURCE.name,A,self.nonce)
    def burst_execution(self):return next(r for r in self.value['results'] if r['label']=='bag_burst_sqlite3')['execution']
    def test_burst_extra_callback(self):
        p=self.root/(B+'.bag_sqlite3_burst.json');v=json.loads(p.read_text());v['received'].append(v['received'][-1]);p.write_text(json.dumps(v));self.reject()
    def test_burst_missing_callback(self):
        p=self.root/(B+'.bag_sqlite3_burst.json');v=json.loads(p.read_text());v['received'].pop();p.write_text(json.dumps(v));self.reject()
    def test_burst_emergency_exit(self):self.burst_execution()['burst_process']['emergency_cleanup']=True;self.reject()
    def test_burst_wrong_stop_nonce(self):
        (self.root/(A+'.bag_sqlite3.burst_stop')).write_text('stale');self.reject()
    def test_burst_wrong_loaded_binary(self):
        hashes=self.burst_execution()['burst_process']['native']['hashes'];hashes['/tmp/librmw_mdds.so']='0'*64;self.reject()
    def test_rehashed_player_reports_four_messages(self):
        execution=self.burst_execution();p=self.root/(A+'.'+execution['log']['path'])
        raw=p.read_text();self.assertIn('Burst 3 messages.',raw)
        p.write_text(raw.replace('Burst 3 messages.','Burst 4 messages.'));execution['log']['sha256']=a.digest(p.read_bytes());self.reject()


if __name__=='__main__':unittest.main(argv=[sys.argv[0]],defaultTest='BurstReceipt')
