"""Adversarial verification of a real CLI-started native node."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
import cli_acceptance as a
from verify_cli_daemon import validate_report

SOURCE=Path(sys.argv[1]).resolve()
A,B=a.TARGET['board_serials']


class ProcessReceipt(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='process_receipt_');self.root=Path(self.temp.name)/SOURCE.name
        shutil.copytree(SOURCE,self.root,ignore=shutil.ignore_patterns('*.tar','*.py','mdds_broker_daemon','mdds_token_exec'))
        for name in ('process_talker.launch.py','peer_talker_test.py'):
            definition=SOURCE/name
            if definition.is_file():shutil.copy2(definition,self.root/definition.name)
        self.value=json.loads((self.root/(A+'.cli.results.json')).read_text());self.nonce=(self.root/'nonce').read_text().strip()
    def tearDown(self):self.temp.cleanup()
    def check(self):validate_report(self.value,self.root,SOURCE.name,A,self.nonce)
    def reject(self):
        with self.assertRaises((ValueError,AssertionError)):self.check()
    def execution(self):return next(r for r in self.value['results'] if r['case_id'] in ('cli:run','cli:launch','cli:test'))['execution']
    def python_execution(self):
        rows=[r['execution'] for r in self.value['results'] if r['label']=='process_run_python']
        if not rows:self.skipTest('fixture has no Python ros2 run path')
        return rows[0]
    def alter_peer_proof(self,stage,change,prefix='process'):
        p=self.root/(B+'.'+prefix+'_'+stage+'.json');value=json.loads(p.read_text());old=json.dumps(value);change(value);p.write_text(json.dumps(value)+'\n')
        log=self.root/(B+'.ros.log');raw=log.read_text();self.assertIn('CLI_PROCESS_PROOF '+old,raw);log.write_text(raw.replace('CLI_PROCESS_PROOF '+old,'CLI_PROCESS_PROOF '+json.dumps(value)))
    def test_original(self):self.check()
    def test_foreign_parent(self):self.execution()['process']['native']['parent_pid']=1;self.reject()
    def test_wrong_group(self):self.execution()['process']['native']['process_group']=1;self.reject()
    def test_wrong_binary_hash(self):self.execution()['process']['native']['hashes']['/tmp/talker']='0'*64;self.reject()
    def test_udp(self):self.execution()['process']['native']['owned_udp']=['socket'];self.reject()
    def test_emergency_cleanup(self):self.execution()['process']['emergency_cleanup']=True;self.reject()
    def test_child_still_alive(self):self.execution()['process']['native_gone']=False;self.reject()
    def test_wrong_barrier(self):(self.root/(A+'.process.stop')).write_text('stale');self.reject()
    def test_missing_peer_messages(self):self.alter_peer_proof('received',lambda v:v.update(received=[]));self.reject()
    def test_wrong_endpoint_type(self):self.alter_peer_proof('received',lambda v:v['endpoint'].update(type='std_msgs/msg/Int32'));self.reject()
    def test_ghost_node(self):self.alter_peer_proof('gone',lambda v:v.update(node_absent=False));self.reject()
    def test_missing_python_command(self):
        self.python_execution();self.value['results']=[r for r in self.value['results'] if r['label']!='process_run_python'];self.reject()
    def test_python_wrong_interpreter(self):self.python_execution()['process']['native']['executable']='/usr/bin/python';self.reject()
    def test_python_shared_launcher(self):self.python_execution()['process']['native']['argv'][1]='/data/local/tmp/ros2/Lib/demo_nodes_py/talker';self.reject()
    def test_python_foreign_native_extension(self):
        hashes=self.python_execution()['process']['native']['hashes'];key=next(k for k in hashes if '_rclpy_pybind11' in k);hashes[key]='0'*64;self.reject()
    def test_python_stale_stop(self):self.python_execution();(self.root/(A+'.python_process.stop')).write_text('stale');self.reject()
    def test_python_package_changed(self):self.python_execution();(self.root/'run_python.zip').write_bytes(b'changed');self.reject()
    def test_python_missing_peer_samples(self):
        self.python_execution();p=self.root/(B+'.python_process_received.json');v=json.loads(p.read_bytes());v['received']=[];p.write_text(json.dumps(v));self.reject()
    def test_python_node_not_withdrawn(self):
        self.python_execution();p=self.root/(B+'.python_process_gone.json');v=json.loads(p.read_bytes());v['node_absent']=False;p.write_text(json.dumps(v));self.reject()
    def test_python_retirement_cannot_use_cpp_identity(self):
        self.python_execution();self.alter_peer_proof('gone',lambda v:v.update(kind='run'),prefix='python_process');self.reject()


if __name__=='__main__':unittest.main(argv=[sys.argv[0]])
