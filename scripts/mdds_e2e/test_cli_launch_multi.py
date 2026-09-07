"""Multi-node launch scope and independent child-exit requirements."""
import ast
import json
from pathlib import Path
import unittest
import cli_acceptance as a
from cli_process import recipe, native_exit_code


class MultiLaunchTest(unittest.TestCase):
    def test_launch_definition_has_two_node_actions(self):
        tree=ast.parse((Path(__file__).parent/'process_talker.launch.py').read_text())
        nodes=[v for v in ast.walk(tree) if isinstance(v,ast.Call) and isinstance(v.func,ast.Name) and v.func.id=='Node']
        self.assertEqual(len(nodes),2)
    def test_recipe_has_distinct_secondary_node_and_topic(self):
        _,_,argv,expected=recipe('/ros_broker_fixture','B',mode='launch')[0]
        self.assertIn('secondary',expected)
        self.assertNotEqual(expected['node'],expected['secondary']['node'])
        self.assertNotEqual(expected['topic'],expected['secondary']['topic'])
        self.assertIn('secondary_node_name:=launch_secondary_A',argv)
        self.assertIn('secondary_output_topic:=/process_fixture/A/secondary_out',argv)
    def test_each_child_exit_is_parsed_independently(self):
        raw='[INFO] [talker-1]: process has finished cleanly [pid 10]\n[ERROR] [talker-2]: process has died [pid 11, exit code -11, cmd x]\n'
        self.assertEqual(native_exit_code(raw,10,label='talker-1'),0)
        self.assertEqual(native_exit_code(raw,11,label='talker-2'),-11)
        self.assertIsNone(native_exit_code(raw,11,label='talker-1'))
    def test_old_single_node_receipt_is_rejected(self):
        root=Path(__file__).resolve().parents[2]/'ohos_test_logs/ros_broker/cli_launch_ffrt_fix_20260907'
        manifest=json.loads((root/'cli_partial_manifest.json').read_bytes())
        case=next(c for c in manifest['cases'] if c['id']=='cli:launch')
        with self.assertRaises(ValueError):a.validate_receipt(case,case['evidence'][0],manifest,root)


if __name__=='__main__':unittest.main()
