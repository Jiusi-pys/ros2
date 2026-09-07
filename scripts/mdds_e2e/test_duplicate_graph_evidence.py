"""Reject unbound or incomplete two-phase native graph observations."""
import json
from pathlib import Path
import tempfile
import unittest
from test_duplicate_graph_contract import sample
from duplicate_graph_contract import payload
from verify_duplicate_graph import validate
import cli_acceptance as a


class Evidence(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
        self.board=a.TARGET['board_serials'][0];self.nonce='abcdef12'*4
        self.value={'duplicate_graph':[]};self.lines=[]
        for phase,name in ((1,'hidden_source.json'),(2,'duplicate_survivor.json')):
            data={};n=int(self.nonce[:7],16)
            for i in ((0,1) if phase==1 else (0,)):
                key='A'+str(i)
                data[key]={'received':[payload('run',self.nonce,'B'+str(i),phase,j) for j in range(3)],'service_sum':n+100+phase,'served':[101] if phase==1 else [101,102]}
                self.lines.append('DUPLICATE_SERVICE_RX '+json.dumps({'key':key,'nonce':self.nonce,'a':n,'b':100+phase,'sum':n+100+phase}))
            record={'run_id':'run','nonce':self.nonce,'role':'A','snapshot':sample(phase),'data':data}
            self.value['duplicate_graph'].append(record)
            (self.root/(self.board+'.'+name)).write_text(json.dumps(record))
            self.lines.append('DUPLICATE_GRAPH_PHASE '+json.dumps(record))
        (self.root/(self.board+'.ros.log')).write_text('\n'.join(self.lines)+'\n')
        for name in ('hidden_source.go','hidden_cli.go','hidden_source.stop','hidden_source.done','hidden_cli.ready','hidden_cli.done'):
            (self.root/(self.board+'.'+name)).write_text(self.nonce+'\n')
    def tearDown(self):self.temp.cleanup()
    def check(self):validate(self.value,self.root,'run',self.board,self.nonce)
    def reject(self):
        with self.assertRaises(ValueError):self.check()
    def test_valid(self):self.check()
    def test_wrong_nonce_barrier(self):(self.root/(self.board+'.hidden_cli.go')).write_text('old');self.reject()
    def test_missing_service_callback(self):
        p=self.root/(self.board+'.ros.log');p.write_text(p.read_text().replace('DUPLICATE_SERVICE_RX ','REMOVED '));self.reject()
    def test_same_board_loopback_substitution(self):
        p=self.root/(self.board+'.ros.log');raw=p.read_text();v=self.value['duplicate_graph'][1]
        old=json.dumps(v);v['data']['A0']['received']=[payload('run',self.nonce,'A0',2,j) for j in range(3)]
        p.write_text(raw.replace(old,json.dumps(v)))
        (self.root/(self.board+'.duplicate_survivor.json')).write_text(json.dumps(v));self.reject()
    def test_second_phase_missing(self):self.value['duplicate_graph'].pop();self.reject()
    def test_retirement_marker_not_optional(self):(self.root/(self.board+'.hidden_source.done')).write_text('');self.reject()


if __name__=='__main__':unittest.main()
