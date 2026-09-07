"""Local graph-case receipts need both real observer process completions."""
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

SOURCE=Path(__file__).resolve().parents[2]/'ohos_test_logs/ros_broker/graph_waiters_20260907_01'
CASE='graph:local_guard'


class LocalGraphGateTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='local_graph_gate_');self.root=Path(self.temp.name)/SOURCE.name
        shutil.copytree(SOURCE,self.root,ignore=shutil.ignore_patterns('*.tar','*.py','mdds_broker_daemon','mdds_token_exec'))
    def tearDown(self):self.temp.cleanup()
    def execute(self):
        with patch.object(sys,'argv',['verify',str(self.root),SOURCE.name]),contextlib.redirect_stdout(io.StringIO()):main()
    def add_unit_terminal(self,board,code=0):
        path=self.root/(board+'.cli.results.json');report=json.loads(path.read_bytes());old=json.dumps(report)
        value=report['graph_waiters'];argv=value['argv']
        raw=(self.root/(board+'.graph_waiters.stdout')).read_text()+'\n'+(self.root/(board+'.graph_waiters.stderr')).read_text()
        log=self.root/(board+'.graph_waiters.log')
        log.write_text('MDDS_GRAPH_ACTUAL_ARGV '+json.dumps(argv)+'\n'+raw+'\n'+a.terminal_marker(SOURCE.name,CASE,code,argv,board)+'\n',newline='\n')
        value['log']={'path':'graph_waiters.log','sha256':a.digest(log.read_bytes())}
        (self.root/(board+'.graph_waiters.json')).write_text(json.dumps(value)+'\n')
        path.write_text(json.dumps(report)+'\n')
        cli=self.root/(board+'.cli.log');cli.write_text(cli.read_text().replace('CLI_DAEMON_RESULT '+old,'CLI_DAEMON_RESULT '+json.dumps(report)))
    def test_missing_terminal_rejected(self):
        with self.assertRaises(ValueError):self.execute()
    def test_single_board_terminal_rejected(self):
        self.add_unit_terminal(a.TARGET['board_serials'][0])
        with self.assertRaises(ValueError):self.execute()
    def test_nonzero_terminal_rejected(self):
        for board in a.TARGET['board_serials']:self.add_unit_terminal(board,1)
        with self.assertRaises(ValueError):self.execute()
    def test_complete_unit_fixture_is_accepted(self):
        for board in a.TARGET['board_serials']:self.add_unit_terminal(board)
        self.execute()
        manifest=json.loads((self.root/'cli_partial_manifest.json').read_bytes())
        case=next(c for c in manifest['cases'] if c['id']==CASE)
        self.assertEqual(case['status'],'PASS')
        a.validate_receipt(case,case['evidence'][0],manifest,self.root)


if __name__=='__main__':unittest.main()
