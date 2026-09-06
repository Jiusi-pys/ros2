"""Reject corrupted daemon lifecycle evidence from a real dual-board run."""
import copy
import json
from pathlib import Path
import sys
import unittest
from verify_cli_daemon import validate_report

ROOT=Path(sys.argv.pop(1)).resolve()
BOARD='3e01ff55454d202020104033bf453b00'
BASE=json.loads((ROOT/(BOARD+'.cli.results.json')).read_text(encoding='utf-8'))


class DaemonReceipt(unittest.TestCase):
    def setUp(self):self.value=copy.deepcopy(BASE)
    def verify(self):validate_report(self.value,ROOT,BASE['run_id'],BOARD,BASE['nonce'])
    def reject(self):
        with self.assertRaises(ValueError):self.verify()
    def test_complete(self):self.verify()
    def test_missing_exit(self):
        self.value['daemon_exited']['terminated']=False;self.reject()
    def test_replaced(self):
        self.value['daemon_after_queries']['start']='1234';self.reject()
    def test_wrong_library(self):
        self.value['daemon']['library_hashes']={};self.reject()
    def test_udp(self):
        self.value['daemon']['owned_udp']=[{'local':'x'}];self.reject()
    def test_foreign_root(self):
        self.value['daemon']['broker_root']='/foreign';self.reject()
    def test_missing_cached_query(self):
        self.value['results']=[r for r in self.value['results'] if r['label']!='nodes_cached'];self.reject()
    def test_direct_substituted_for_cached(self):
        next(r for r in self.value['results'] if r['label']=='nodes_cached')['execution']['argv'].append('--no-daemon');self.reject()
    def test_emergency_cleanup(self):
        self.value['emergency_cleanup']={'terminated':True};self.reject()
    def test_daemon_remains(self):
        self.value['after']['daemons']=[{'pid':123}];self.reject()
    def test_missing_hidden_services(self):
        self.value['results']=[r for r in self.value['results'] if r['label']!='service_find_hidden'];self.reject()
    def test_wrong_service_count(self):
        next(r for r in self.value['results'] if r['label']=='service_info_cached')['expected']['Clients count']='0';self.reject()
    def test_cached_substituted_for_direct_service_info(self):
        next(r for r in self.value['results'] if r['label']=='service_info_direct')['execution']['argv'].remove('--no-daemon');self.reject()


if __name__=='__main__':unittest.main()
