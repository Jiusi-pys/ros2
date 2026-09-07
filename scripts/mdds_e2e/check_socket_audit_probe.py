"""Audit instrumentation cannot pass using missing, forged or partial counters."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from cli_acceptance import TARGET
from verify_socket_audit_probe import check
SOURCE=Path(sys.argv[1]).resolve();A,B=TARGET['board_serials']
class AuditProbe(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)/SOURCE.name;shutil.copytree(SOURCE,self.root)
    def tearDown(self):self.temp.cleanup()
    def reject(self):
        with self.assertRaises((ValueError,KeyError,FileNotFoundError)):check(self.root)
    def log_change(self,board,mode,old,new):
        p=self.root/(board+'.'+mode+'.log');p.write_text(p.read_text().replace(old,new))
    def result_change(self,board,mode,change):
        p=self.root/(board+'.'+mode+'.result.json');v=json.loads(p.read_bytes());old=json.dumps(v);change(v);p.write_text(json.dumps(v));self.log_change(board,mode,old,json.dumps(v))
    def test_original(self):self.assertTrue(check(self.root)['passed'])
    def test_final_interval_required(self):self.log_change(B,'positive','MDDS_SOCKET_AUDIT_FINAL ','REMOVED ');self.reject()
    def test_load_required(self):self.log_change(A,'zero','MDDS_SOCKET_AUDIT_LOADED ','REMOVED ');self.reject()
    def test_positive_cannot_be_zero(self):self.result_change(A,'positive',lambda v:v['after'].update(ipv4_datagram_calls=0));self.reject()
    def test_real_socket_success_required(self):self.result_change(B,'positive',lambda v:v['calls'][4].update(fd=-1));self.reject()
    def test_errno_preserved(self):self.result_change(A,'zero',lambda v:v.update(failed_errno=0));self.reject()
    def test_exact_library_required(self):self.result_change(A,'positive',lambda v:v.update(library_sha256='0'*64));self.reject()
    def test_process_start_required(self):self.result_change(A,'zero',lambda v:v.update(start='1'));self.reject()
    def test_missing_control_required(self):self.log_change(B,'missing','SOCKET_AUDIT_MISSING ','REMOVED ');self.reject()
    def test_native_terminal_required(self):self.log_change(A,'zero','GRAPH_PROCESS_EXIT ','REMOVED ');self.reject()
if __name__=='__main__':unittest.main(argv=[sys.argv[0]])
