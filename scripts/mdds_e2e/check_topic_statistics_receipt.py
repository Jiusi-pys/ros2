"""Adversarial checks against a real topic statistics receipt directory."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
import cli_acceptance as a
from verify_cli_daemon import validate_report

SOURCE=Path(sys.argv[1]).resolve()
A=a.TARGET['board_serials'][0]
B=a.TARGET['board_serials'][1]


class StatisticsReceipt(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='statistics_receipt_');self.root=Path(self.temp.name)/SOURCE.name
        shutil.copytree(SOURCE,self.root,ignore=shutil.ignore_patterns('*.tar','*.py','mdds_broker_daemon','mdds_token_exec'))
        self.value=json.loads((self.root/(A+'.cli.results.json')).read_text());self.nonce=(self.root/'nonce').read_text().strip()
    def tearDown(self):self.temp.cleanup()
    def check(self):validate_report(self.value,self.root,SOURCE.name,A,self.nonce)
    def reject(self):
        with self.assertRaises((ValueError,AssertionError)):self.check()
    def execution(self,verb='hz'):return next(r for r in self.value['results'] if r['label']=='topic_'+verb)['execution']
    def alter_proof(self,board,verb,stage,change):
        p=self.root/(board+'.stats_'+verb+'_'+stage+'.json');value=json.loads(p.read_text());old=json.dumps(value);change(value);p.write_text(json.dumps(value)+'\n')
        log=self.root/(board+'.ros.log');raw=log.read_text();self.assertIn('CLI_STATS_PROOF '+old,raw);log.write_text(raw.replace('CLI_STATS_PROOF '+old,'CLI_STATS_PROOF '+json.dumps(value)))
    def test_original(self):self.check()
    def test_missing_source_sample(self):self.alter_proof(B,'hz','sent',lambda v:v['data'].pop());self.reject()
    def test_wrong_payload_despite_matching_log(self):self.alter_proof(A,'hz','received',lambda v:v['data'].__setitem__(0,'wrong'));self.reject()
    def test_wrong_raw_size(self):self.alter_proof(A,'bw','received',lambda v:v.update(sizes=[1]*20));self.reject()
    def test_wrong_receiver_stamp(self):self.alter_proof(A,'delay','received',lambda v:v.update(stamp_ns=1));self.reject()
    def test_reversed_receive_clock(self):self.alter_proof(A,'hz','received',lambda v:v['receive_ns'].reverse());self.reject()
    def test_wrong_native_prefix(self):self.execution()['statistics_process']['native']['hashes']['/tmp/librmw_mdds.so']='0'*64;self.reject()
    def test_udp(self):self.execution()['statistics_process']['native']['owned_udp']=['socket'];self.reject()
    def test_emergency_exit(self):self.execution()['statistics_process']['emergency_cleanup']=True;self.reject()
    def test_timeout_cannot_be_success(self):self.execution()['returncode']=124;self.reject()
    def test_fabricated_pre_signal_observation(self):self.execution()['statistics_process']['observed_stdout']='fabricated';self.reject()


if __name__=='__main__':unittest.main(argv=[sys.argv[0]])
