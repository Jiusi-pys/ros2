"""Launch-specific adversaries in addition to native process lifecycle checks."""
import sys
import unittest
import cli_acceptance as a
from check_cli_process_receipt import ProcessReceipt,A


class LaunchReceipt(ProcessReceipt):
    def test_requires_launcher_to_forward_stop(self):self.execution()['process']['shutdown']['recipient']='group';self.reject()
    def test_rehashed_missing_forwarding_log(self):
        e=self.execution();p=self.root/(A+'.'+e['log']['path']);raw=p.read_text();needle="sending signal 'SIGINT' to process[talker-1]"
        self.assertIn(needle,raw);p.write_text(raw.replace(needle,'missing forwarding'),newline='\n');e['log']['sha256']=a.digest(p.read_bytes());self.reject()
    def test_malformed_stdout_fails_as_invalid_evidence(self):
        e=self.execution();p=self.root/(A+'.'+e['log']['path']);raw=p.read_text()
        p.write_text(raw.replace('MDDS_CLI_STDOUT_BEGIN','MISSING_STDOUT_BEGIN'),newline='\n');e['log']['sha256']=a.digest(p.read_bytes());self.reject()
    def test_launch_definition_cannot_change_after_run(self):(self.root/'process_talker.launch.py').write_text('changed');self.reject()


if __name__=='__main__':unittest.main(argv=[sys.argv[0]],defaultTest='LaunchReceipt')
