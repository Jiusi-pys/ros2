"""The formal local graph case must retain its post-exit evidence."""
import json
import sys
import unittest
import cli_acceptance as a
from cli_graph_waiters import emit_receipt,CASE
from check_graph_waiters_receipt import GraphWaiterReceipt,A,B,SOURCE


class LocalGraphReceipt(GraphWaiterReceipt):
    def check(self):
        super().check()
        manifest=json.loads((self.root/'cli_partial_manifest.json').read_bytes())
        reports={A:self.value,B:json.loads((self.root/(B+'.cli.results.json')).read_bytes())}
        self.assertEqual(emit_receipt(self.root,manifest,reports,SOURCE.name,self.nonce),CASE)
    def alter_log(self,replace):
        value=self.value['graph_waiters'];p=self.root/(A+'.'+value['log']['path']);p.write_text(replace(p.read_text()),newline='\n')
        value['log']['sha256']=a.digest(p.read_bytes())
        (self.root/(A+'.graph_waiters.json')).write_text(json.dumps(value)+'\n')
    def test_missing_native_terminal(self):
        self.alter_log(lambda raw:raw.replace('MDDS_CLI_TERMINAL ','REMOVED '));self.reject()
    def test_nonzero_native_terminal(self):
        self.alter_log(lambda raw:raw.replace('"returncode":0','"returncode":1'));self.reject()
    def test_native_argv_missing(self):
        self.alter_log(lambda raw:raw.replace('MDDS_GRAPH_ACTUAL_ARGV ','REMOVED '));self.reject()
    def test_log_cannot_replace_original_cpp_output(self):
        self.alter_log(lambda raw:raw.replace('"second":true','"second":false'));self.reject()


if __name__=='__main__':unittest.main(argv=[sys.argv[0]],defaultTest='LocalGraphReceipt')
