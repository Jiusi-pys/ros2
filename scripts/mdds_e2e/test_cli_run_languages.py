"""Both installed demo languages are required for ros2 run acceptance."""
import json
from pathlib import Path
import unittest
import tempfile
import contextlib
import io
import cli_acceptance as a
from cli_process import recipe, native_matches
from board_process_probe import ProcessProbe


class RunLanguagesTest(unittest.TestCase):
    def test_retirement_proof_identifies_demo_kind(self):
        with tempfile.TemporaryDirectory() as directory,contextlib.redirect_stdout(io.StringIO()):
            values=[]
            for kind,prefix in (('run','process'),('run_python','python_process')):
                probe=ProcessProbe.__new__(ProcessProbe)
                probe.root=Path(directory);probe.run='fixture';probe.nonce='a'*32;probe.board='board';probe.peer='A';probe.kind=kind;probe.prefix=prefix
                probe.save('gone',node_absent=True,publisher_absent=True)
                values.append(json.loads((probe.root/(prefix+'_gone.json')).read_bytes()))
            self.assertNotEqual(values[0],values[1])
            self.assertEqual([v['kind'] for v in values],['run','run_python'])
    def test_recipe_executes_both_demos(self):
        rows = recipe('/ros_broker_fixture', 'B')
        self.assertEqual([row[2][1:3] for row in rows], [['demo_nodes_cpp', 'talker'], ['demo_nodes_py', 'talker']])
    def test_cpp_only_historical_receipt_is_rejected(self):
        root = Path(__file__).resolve().parents[2] / 'ohos_test_logs/ros_broker/cli_run_ffrt_fix_20260907'
        manifest = json.loads((root/'cli_partial_manifest.json').read_bytes())
        case = next(c for c in manifest['cases'] if c['id'] == 'cli:run')
        with self.assertRaises(ValueError): a.validate_receipt(case, case['evidence'][0], manifest, root)
    def test_python_owned_child(self):
        root='/data/local/tmp/ros2/.mdds-owned-runs/fixture'
        expected={'language':'python','native_args':['--ros-args','-r','__node:=run_python_A']}
        record={'pid':12,'start':'123','parent_pid':11,'process_group':11,
                'executable':'/data/python312-rk3588a/usr/bin/python3.12',
                'argv':['python3.12',root+'/execution_prefix/lib/demo_nodes_py/talker']+expected['native_args']}
        self.assertTrue(native_matches(record,root,11,expected))
        record['argv'][1]='/data/local/tmp/ros2/Lib/demo_nodes_py/talker'
        self.assertFalse(native_matches(record,root,11,expected))


if __name__ == '__main__': unittest.main()
