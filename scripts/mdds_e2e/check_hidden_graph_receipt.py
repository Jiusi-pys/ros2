"""Reject hidden-name leakage, incomplete raw graph data and relabelled views."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
import cli_acceptance as a
from verify_cli_daemon import validate_report
from verify_hidden_graph import emit
from hidden_graph_contract import scope

SOURCE=Path(sys.argv[1]).resolve();A,B=a.TARGET['board_serials']


class HiddenGraphReceipt(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='hidden_graph_receipt_');self.root=Path(self.temp.name)/SOURCE.name
        shutil.copytree(SOURCE,self.root,ignore=shutil.ignore_patterns('*.tar','*.py','mdds_broker_daemon','mdds_token_exec'))
        self.value=json.loads((self.root/(A+'.cli.results.json')).read_bytes());self.nonce=(self.root/'nonce').read_text().strip()
    def tearDown(self):self.temp.cleanup()
    def check(self):
        validate_report(self.value,self.root,SOURCE.name,A,self.nonce)
        reports={A:self.value,B:json.loads((self.root/(B+'.cli.results.json')).read_bytes())}
        manifest=json.loads((self.root/'cli_partial_manifest.json').read_bytes());self.assertEqual(emit(self.root,manifest,reports,SOURCE.name,self.nonce),'graph:hidden_entities')
    def reject(self):
        with self.assertRaises(ValueError):self.check()
    def alter_log(self,kind,hidden,change):
        result=next(v for v in self.value['hidden_graph']['results'] if v['kind']==kind and v['hidden']==hidden);e=result['execution']
        p=self.root/(A+'.'+e['log']['path']);p.write_text(change(p.read_text()),newline='\n');e['log']['sha256']=a.digest(p.read_bytes())
        (self.root/(A+'.hidden_cli.json')).write_text(json.dumps(self.value['hidden_graph'])+'\n')
    def test_original(self):self.check()
    def test_hidden_action_leak(self):
        name=scope(SOURCE.name)+'/A/_secret/action [example_interfaces/action/Fibonacci]\n'
        self.alter_log('action',False,lambda raw:raw.replace('MDDS_CLI_STDOUT_BEGIN\n','MDDS_CLI_STDOUT_BEGIN\n'+name));self.reject()
    def test_hidden_node_leak(self):
        self.alter_log('node',False,lambda raw:raw.replace('MDDS_CLI_STDOUT_BEGIN\n','MDDS_CLI_STDOUT_BEGIN\n'+scope(SOURCE.name)+'/A/_hidden\n'));self.reject()
    def test_hidden_service_leak(self):
        name=scope(SOURCE.name)+'/A/_secret/serve [example_interfaces/srv/AddTwoInts]\n'
        self.alter_log('service',False,lambda raw:raw.replace('MDDS_CLI_STDOUT_BEGIN\n','MDDS_CLI_STDOUT_BEGIN\n'+name));self.reject()
    def test_explicit_hidden_view_must_be_complete(self):
        name=scope(SOURCE.name)+'/A/_secret/action [example_interfaces/action/Fibonacci]\n'
        self.alter_log('action',True,lambda raw:raw.replace(name,''));self.reject()
    def test_native_service_names_must_be_preserved(self):
        p=self.root/(A+'.hidden_source.json');v=json.loads(p.read_bytes());key=next(k for k in v['snapshot']['native_topics'] if k.startswith('rq'));del v['snapshot']['native_topics'][key];p.write_text(json.dumps(v));self.reject()
    def test_raw_by_node_is_required(self):
        p=self.root/(A+'.hidden_source.json');v=json.loads(p.read_bytes());v['snapshot']['native_by_node']={};p.write_text(json.dumps(v));self.reject()
    def test_actual_flag_cannot_be_changed(self):
        result=next(v for v in self.value['hidden_graph']['results'] if v['kind']=='action' and v['hidden']);result['execution']['argv'].pop();self.reject()
    def test_native_cli_failure(self):self.value['hidden_graph']['results'][0]['execution']['returncode']=1;self.reject()
    def test_source_cleanup_must_complete(self):(self.root/(A+'.hidden_source.done')).write_text('old');self.reject()
    def test_terminal_required(self):self.alter_log('topic',True,lambda raw:raw.replace('MDDS_CLI_TERMINAL ','REMOVED '));self.reject()


if __name__=='__main__':unittest.main(argv=[sys.argv[0]])
