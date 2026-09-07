"""Adversarial checks for the actual conversion and reindex receipt."""
import json
import sqlite3
from contextlib import closing
import sys
import unittest
from check_bag_receipt import BagReceipt,A,SOURCE
from verify_bag_transform import validate


class TransformReceipt(BagReceipt):
    def check(self):
        super().check()
        validate(self.value,self.root,SOURCE.name,A)
    def test_reindex_requires_initial_metadata_absence(self):
        record=next(r for r in self.value['results'] if r['label']=='bag_reindex_sqlite3')
        record['transform_preparation']['metadata_absent']=False;self.reject()
    def test_conversion_rejects_rehashed_retiming(self):
        p=self.root/(A+'.bags_converted_sqlite3_converted_sqlite3_0.db3')
        with closing(sqlite3.connect(p)) as db, db:db.execute('UPDATE messages SET timestamp=timestamp+1')
        self.update_hash(p);self.reject()
    def test_reindex_rejects_rehashed_changed_data(self):
        p=self.root/(A+'.bags_reindex_sqlite3_sqlite3_0.db3')
        with closing(sqlite3.connect(p)) as db, db:db.execute('UPDATE messages SET data=? WHERE id=(SELECT min(id) FROM messages)',(b'wrong',))
        self.update_hash(p);self.reject()
    def test_conversion_options_are_verified(self):
        p=self.root/(A+'.bag_convert_sqlite3_to_mcap.yaml');p.write_text('output_bags: []\n');self.reject()


if __name__=='__main__':unittest.main(argv=[sys.argv[0]],defaultTest='TransformReceipt')
