"""Remote guards require opposite-board mutation evidence for every phase."""
import json
import sys
import unittest
import cli_acceptance as a
from cli_graph_waiters import emit_receipt
from check_graph_waiters_receipt import GraphWaiterReceipt,A,B,SOURCE


class RemoteGraphReceipt(GraphWaiterReceipt):
    def check(self):
        super().check()
        manifest=json.loads((self.root/'cli_partial_manifest.json').read_bytes())
        reports={A:self.value,B:json.loads((self.root/(B+'.cli.results.json')).read_bytes())}
        self.assertEqual(emit_receipt(self.root,manifest,reports,SOURCE.name,self.nonce),'graph:remote_guard')
    def alter_source(self,index,change):
        p=self.root/(B+f'.remote_source_{index}.json');value=json.loads(p.read_bytes());old=json.dumps(value);change(value);p.write_text(json.dumps(value)+'\n')
        log=self.root/(B+'.ros.log');raw=log.read_text();self.assertIn('GRAPH_REMOTE_SOURCE '+old,raw);log.write_text(raw.replace('GRAPH_REMOTE_SOURCE '+old,'GRAPH_REMOTE_SOURCE '+json.dumps(value)))
    def test_local_mutation_not_accepted(self):self.change_output('GRAPH_WAITER_SCOPE remote','GRAPH_WAITER_SCOPE local')
    def test_remote_wait_must_be_armed(self):self.change_output('GRAPH_REMOTE_ARMED 0 ','REMOVED 0 ')
    def test_source_must_be_opposite_board(self):self.alter_source(0,lambda v:v.update(source_role='A'));self.reject()
    def test_source_phase_must_match(self):self.alter_source(0,lambda v:v.update(index=1));self.reject()
    def test_node_must_be_bare(self):self.alter_source(8,lambda v:v.update(bare_node=False));self.reject()
    def test_stale_source_control(self):(self.root/(B+'.remote_change_0.go')).write_text('old');self.reject()
    def test_stale_source_completion(self):(self.root/(B+'.remote_source_0.done')).write_text('old');self.reject()
    def test_stale_observer_ack(self):(self.root/(A+'.remote_applied_0.done')).write_text('old');self.reject()
    def test_terminal_cannot_be_relabelled_local(self):
        value=self.value['graph_waiters'];p=self.root/(A+'.'+value['log']['path']);p.write_text(p.read_text().replace('graph:remote_guard','graph:local_guard'),newline='\n')
        value['log']['sha256']=a.digest(p.read_bytes());(self.root/(A+'.graph_waiters.json')).write_text(json.dumps(value)+'\n');self.reject()


if __name__=='__main__':unittest.main(argv=[sys.argv[0]],defaultTest='RemoteGraphReceipt')
