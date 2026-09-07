import json
from pathlib import Path
import tempfile
import unittest
import cli_acceptance as a


class ControlledStop(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
        self.case={'id':'cli:service/echo','command':['ros2','service','echo'],'execution_boards':a.TARGET['board_serials'][:1],'assertions':['functional_result']}
        self.manifest={'run_id':'stop_test'}
        self.stdout='FUNCTIONAL_EVENTS exact\n'
        self.execution={'argv':['ros2','service','echo','/fixture','example_interfaces/srv/AddTwoInts'],
            'board_serial':a.TARGET['board_serials'][0],'returncode':2,'child_pid':123,'child_start':'456',
            'controlled_stop':{'signal':2,'reason':'functional_observed','child_pid':123,'child_start':'456','stdout_sha256':a.digest(self.stdout.encode())}}
    def validate(self, omit_marker=False):
        e=self.execution;case=self.case
        raw='MDDS_CLI_STDOUT_BEGIN\n'+self.stdout+'\nMDDS_CLI_STDOUT_END\n'
        if 'controlled_stop' in e and not omit_marker:raw+=a.controlled_stop_marker('stop_test',case['id'],e)+'\n'
        raw+=a.terminal_marker('stop_test',case['id'],e['returncode'],e['argv'],e['board_serial'])+'\n'
        p=self.root/'raw.log';p.write_text(raw,encoding='utf-8',newline='\n');e['log']={'path':p.name,'sha256':a.digest(p.read_bytes())}
        receipt={'schema_version':1,'run_id':'stop_test','case_id':case['id'],'kind':'functional','status':'PASS','board_serials':a.TARGET['board_serials'],
            'rmw_implementation':'rmw_mdds','transport':'dsoftbus','executions':[e],'assertions':[{'id':'functional_result','passed':True,'execution':0,'pattern':'FUNCTIONAL_EVENTS exact'}]}
        p=self.root/'receipt.json';p.write_text(json.dumps(receipt),encoding='utf-8')
        a.validate_receipt(case,{'path':p.name,'sha256':a.digest(p.read_bytes())},self.manifest,self.root)
    def reject(self,**kwargs):
        code=self.execution['returncode']
        for value in ([code,0] if code==2 else [code]):
            self.execution['returncode']=value
            with self.subTest(returncode=value),self.assertRaises(ValueError):self.validate(**kwargs)
    def test_requested_sigint(self):self.validate()
    def test_hello_commands_support_verified_stop(self):
        for command in ('doctor','wtf'):
            self.case.update(id='cli:'+command+'/hello',command=['ros2',command,'hello'])
            self.execution['argv']=['ros2',command,'hello','--topic','/fixture']
            self.validate()
    def test_statistics_commands_support_requested_sigint(self):
        for verb in ('hz','bw','delay'):
            with self.subTest(verb=verb):
                self.case.update(id='cli:topic/'+verb,command=['ros2','topic',verb])
                self.execution['argv']=['ros2','topic',verb,'/fixture']
                self.validate()
    def test_statistics_cannot_claim_unrequested_stop(self):
        self.execution.pop('controlled_stop')
        for verb in ('hz','bw','delay'):
            with self.subTest(verb=verb):
                self.case.update(id='cli:topic/'+verb,command=['ros2','topic',verb])
                self.execution.update(argv=['ros2','topic',verb,'/fixture'],returncode=2)
                self.reject()
    def test_clean_exit_after_requested_stop(self):self.execution['returncode']=0;self.validate()
    def test_unrequested(self):self.execution.pop('controlled_stop');self.reject()
    def test_other_command(self):self.case['id']='cli:service/call';self.reject()
    def test_wrong_pid(self):self.execution['controlled_stop']['child_pid']=9;self.reject()
    def test_wrong_start(self):self.execution['controlled_stop']['child_start']='9';self.reject()
    def test_wrong_output(self):self.execution['controlled_stop']['stdout_sha256']='0'*64;self.reject()
    def test_timeout(self):self.execution['returncode']=124;self.reject()
    def test_missing_marker(self):self.reject(omit_marker=True)


if __name__=='__main__':unittest.main()
