"""Reject incomplete, early, misattributed or unbound late observer evidence."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
import cli_acceptance as a
from verify_cli_daemon import validate_report
from verify_late_graph import emit

SOURCE=Path(sys.argv[1]).resolve();A,B=a.TARGET['board_serials']


class LateGraphReceipt(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='late_graph_receipt_');self.root=Path(self.temp.name)/SOURCE.name
        shutil.copytree(SOURCE,self.root,ignore=shutil.ignore_patterns('*.tar','*.py','mdds_broker_daemon','mdds_token_exec'))
        self.value=json.loads((self.root/(A+'.cli.results.json')).read_bytes());self.nonce=(self.root/'nonce').read_text().strip()
    def tearDown(self):self.temp.cleanup()
    def check(self):
        validate_report(self.value,self.root,SOURCE.name,A,self.nonce)
        reports={A:self.value,B:json.loads((self.root/(B+'.cli.results.json')).read_bytes())}
        manifest=json.loads((self.root/'cli_partial_manifest.json').read_bytes());emit(self.root,manifest,reports,SOURCE.name,self.nonce)
    def reject(self):
        with self.assertRaises((ValueError,FileNotFoundError)):self.check()
    def observer(self):return self.value['late_graph']['observation']
    def mutate_observer(self,change):
        value=self.observer();change(value);(self.root/(A+'.late_observer.json')).write_text(json.dumps(value)+'\n')
    def test_original(self):self.check()
    def test_observer_cannot_start_early(self):self.mutate_observer(lambda v:v.update(started_ns=0));self.reject()
    def test_new_context_required(self):self.mutate_observer(lambda v:v.update(initialized_ns=0));self.reject()
    def test_ready_snapshot_cannot_change(self):
        p=self.root/(B+'.late_source.json');v=json.loads(p.read_bytes());v['ready_ns']+=1;p.write_text(json.dumps(v));self.reject()
    def test_wrong_source_proof(self):self.mutate_observer(lambda v:v.update(peer_source_sha256='0'*64));self.reject()
    def test_missing_source_node(self):self.mutate_observer(lambda v:v['snapshot']['nodes'].pop());self.reject()
    def test_endpoint_owner_cannot_change(self):
        self.mutate_observer(lambda v:next(iter(v['snapshot']['endpoints']['one'].values())).update(node='two'));self.reject()
    def test_endpoint_metadata_must_be_complete(self):
        self.mutate_observer(lambda v:v['snapshot']['endpoints']['two'].popitem());self.reject()
    def test_node_types_cannot_be_inherited(self):
        self.mutate_observer(lambda v:v['snapshot']['sections']['one']['services'].update(v['snapshot']['sections']['two']['services']));self.reject()
    def test_udp_cannot_satisfy_graph(self):self.mutate_observer(lambda v:v['provenance'].update(owned_udp_sockets=['socket']));self.reject()
    def test_process_must_exit(self):self.value['late_graph']['returncode']=1;self.reject()
    def test_source_cleanup_required(self):(self.root/(A+'.late_source.done')).write_text('old');self.reject()
    def test_terminal_required(self):
        value=self.value['late_graph'];p=self.root/(A+'.late_observer.log');p.write_text(p.read_text().replace('MDDS_CLI_TERMINAL ','REMOVED '),newline='\n')
        value['log']['sha256']=a.digest(p.read_bytes());(self.root/(A+'.late_observer.status.json')).write_text(json.dumps({k:v for k,v in value.items() if k!='observation'}));self.reject()


if __name__=='__main__':unittest.main(argv=[sys.argv[0]])
