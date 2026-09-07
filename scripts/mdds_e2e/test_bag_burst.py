import copy
import unittest
from bag_contract import payloads
from bag_burst import check_proof


class BurstProof(unittest.TestCase):
    def setUp(self):
        self.value={'run_id':'test','nonce':'nonce','board':'A','storage':'sqlite3','stage':'burst','received':payloads('test','nonce','A','sqlite3','main')[:3]}
    def check(self):check_proof(self.value,'test','nonce','A','sqlite3')
    def test_exact_three(self):self.check()
    def test_extra_sample(self):
        self.value['received'].append(payloads('test','nonce','A','sqlite3','main')[3])
        with self.assertRaises(ValueError):self.check()
    def test_missing_sample(self):
        self.value['received'].pop()
        with self.assertRaises(ValueError):self.check()
    def test_reordered(self):
        self.value['received'].reverse()
        with self.assertRaises(ValueError):self.check()
    def test_other_run(self):
        self.value['run_id']='stale'
        with self.assertRaises(ValueError):self.check()


if __name__=='__main__':unittest.main()
