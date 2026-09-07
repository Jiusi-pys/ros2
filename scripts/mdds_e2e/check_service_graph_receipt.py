"""Real graph-case evidence must retain its declared scope and native exits."""
import json
import sys
import unittest
from check_service_qos_receipt import ServiceQoSReceipt,A,B,SOURCE
from service_graph_receipt import emit,CASE


class ServiceGraphReceipt(ServiceQoSReceipt):
    def check(self):
        super().check()
        manifest=json.loads((self.root/'cli_partial_manifest.json').read_bytes())
        reports={A:self.value,B:json.loads((self.root/(B+'.cli.results.json')).read_bytes())}
        if emit(self.root,manifest,reports,SOURCE.name,self.nonce)!=CASE:raise ValueError('graph case not issued')
    def test_missing_graph_declaration(self):(self.root/'graph_case').unlink();self.reject()
    def test_wrong_graph_declaration(self):(self.root/'graph_case').write_text('graph:churn\n');self.reject()
    def test_changed_declaration_bytes(self):
        p=self.root/'graph_case';p.write_text(p.read_text()+'\n');self.reject()
    def test_missing_peer_terminal(self):
        p=self.root/(B+'.ros.log');p.write_text(p.read_text().replace('MDDS_CLI_TERMINAL ','REMOVED '));self.reject()
    def test_duplicate_terminal(self):
        p=self.root/(A+'.ros.log');raw=p.read_text();line=next(x for x in raw.splitlines() if x.startswith('MDDS_CLI_TERMINAL '));p.write_text(raw+line+'\n');self.reject()
    def test_nonzero_native_exit(self):
        p=self.root/(A+'.ros.status.json');v=json.loads(p.read_bytes());v['returncode']=139;p.write_text(json.dumps(v));self.reject()
    def test_wrong_process_start(self):
        p=self.root/(A+'.ros.status.json');v=json.loads(p.read_bytes());v['child_start']='1';p.write_text(json.dumps(v));self.reject()
    def test_missing_actual_argv(self):
        p=self.root/(A+'.ros.log');p.write_text(p.read_text().replace('MDDS_GRAPH_ACTUAL_ARGV ','REMOVED '));self.reject()


if __name__=='__main__':unittest.main(argv=[sys.argv[0]],defaultTest='ServiceGraphReceipt')
