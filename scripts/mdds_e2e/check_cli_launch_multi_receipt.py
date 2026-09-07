"""Both launched native children need independent peer and exit evidence."""
import json
import sys
import unittest
import cli_acceptance as a
from check_cli_launch_receipt import LaunchReceipt
from check_cli_process_receipt import A,B


class MultiLaunchReceipt(LaunchReceipt):
    def secondary(self): return self.execution()['process']['children'][1]
    def test_missing_secondary_child(self): self.execution()['process']['children'].pop(); self.reject()
    def test_secondary_crash_cannot_be_masked_by_cli_zero(self): self.secondary()['native_returncode']=-11; self.reject()
    def test_duplicate_child_pid(self): self.secondary()['native']['pid']=self.execution()['process']['children'][0]['native']['pid']; self.reject()
    def test_secondary_wrong_binary(self): self.secondary()['native']['executable']='/tmp/foreign'; self.reject()
    def test_secondary_survives(self): self.secondary()['native_gone']=False; self.reject()
    def test_secondary_forwarding_log_missing(self):
        e=self.execution();p=self.root/(A+'.'+e['log']['path']);raw=p.read_text();needle="sending signal 'SIGINT' to process[talker-2]"
        self.assertIn(needle,raw);p.write_text(raw.replace(needle,'missing secondary forwarding'),newline='\n');e['log']['sha256']=a.digest(p.read_bytes());self.reject()
    def test_secondary_endpoint_cannot_reuse_primary_gid(self):
        primary=json.loads((self.root/(B+'.process_received.json')).read_bytes())
        self.alter_peer_proof('received',lambda v:v['endpoint'].update(gid=primary['endpoint']['gid']),prefix='secondary_process');self.reject()
    def test_secondary_retirement_cannot_reuse_primary_kind(self):
        self.alter_peer_proof('gone',lambda v:v.update(kind='launch'),prefix='secondary_process');self.reject()
    def test_secondary_missing_peer_samples(self):
        self.alter_peer_proof('received',lambda v:v.update(received=[]),prefix='secondary_process');self.reject()


if __name__=='__main__':
    # This fixture launches two C++ nodes. Python ros2 run tests have their own
    # separately executed 20-case suite; do not report them as launch skips.
    names=[n for n in unittest.defaultTestLoader.getTestCaseNames(MultiLaunchReceipt)
           if not n.startswith('test_python') and n!='test_missing_python_command']
    suite=unittest.TestSuite(MultiLaunchReceipt(name) for name in names)
    result=unittest.TextTestRunner().run(suite)
    raise SystemExit(not result.wasSuccessful())
