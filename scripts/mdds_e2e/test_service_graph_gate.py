"""A requested graph case needs actual per-board post-exit terminal evidence."""
import contextlib
import io
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch
import cli_acceptance as a
from verify_cli_daemon import main

SOURCE=Path(__file__).resolve().parents[2]/'ohos_test_logs/ros_broker/service_qos_20260907_01'
CASE='graph:service_client_ownership'


class ServiceGraphGateTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='graph_service_gate_');self.root=Path(self.temp.name)/SOURCE.name
        shutil.copytree(SOURCE,self.root,ignore=shutil.ignore_patterns('*.tar','*.py','mdds_broker_daemon','mdds_token_exec'))
        self.nonce=(self.root/'nonce').read_text().strip();(self.root/'graph_case').write_text(CASE+'\n',newline='\n')
        for board in a.TARGET['board_serials']:
            with (self.root/('inputs_'+board+'.sha256')).open('a') as stream:stream.write(a.digest((self.root/'graph_case').read_bytes())+'  graph_case\n')
    def tearDown(self):self.temp.cleanup()
    def execute(self):
        with patch.object(sys,'argv',['verify',str(self.root),SOURCE.name]),contextlib.redirect_stdout(io.StringIO()):main()
    def add_unit_terminal(self,board,code=0):
        peer=next(b for b in a.TARGET['board_serials'] if b!=board);role='A' if board==a.TARGET['board_serials'][0] else 'B'
        remote='/data/local/tmp/ros2/.mdds-owned-runs/'+SOURCE.name
        argv=['/data/python312-rk3588a/usr/bin/python3.12',remote+'/ros_broker_probe.py','--root',remote,'--run-id',SOURCE.name,'--role',role,'--self-serial',board,'--peer-serial',peer,'--nonce',self.nonce,'--manifest-sha',a.digest((self.root/'rclpy_package.json').read_bytes())]
        with (self.root/(board+'.ros.log')).open('a') as stream:
            stream.write('MDDS_GRAPH_ACTUAL_ARGV '+json.dumps(argv)+'\n'+a.terminal_marker(SOURCE.name,CASE,code,argv,board)+'\n')
    def test_requested_case_without_terminal_is_rejected(self):
        with self.assertRaises(ValueError):self.execute()
    def test_one_board_terminal_is_insufficient(self):
        self.add_unit_terminal(a.TARGET['board_serials'][0])
        with self.assertRaises(ValueError):self.execute()
    def test_nonzero_terminal_is_rejected(self):
        for board in a.TARGET['board_serials']:self.add_unit_terminal(board,1)
        with self.assertRaises(ValueError):self.execute()
    def test_complete_unit_fixture_gets_graph_receipt(self):
        for board in a.TARGET['board_serials']:self.add_unit_terminal(board)
        self.execute()
        manifest=json.loads((self.root/'cli_partial_manifest.json').read_bytes())
        case=next(c for c in manifest['cases'] if c['id']==CASE)
        self.assertEqual(case['status'],'PASS')
        a.validate_receipt(case,case['evidence'][0],manifest,self.root)


if __name__=='__main__':unittest.main()
