"""Adversarial checks against an actual accepted ROS broker receipt directory."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from verify_ros_broker import validate
SOURCE = Path(sys.argv[1]).resolve()
RUN = SOURCE.name
A = '3e01ff55454d202020104033bf453b00'
B = '3e01ff55454d202020104433991c3b00'

class Receipt(unittest.TestCase):

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='ros_receipt_')
        self.root = Path(self.temp.name) / RUN
        shutil.copytree(SOURCE, self.root, ignore=shutil.ignore_patterns('*.tar', 'libmdds.so', 'librmw_mdds.so', '*.py'))

    def tearDown(self):
        self.temp.cleanup()

    def result(self):
        return validate(self.root, RUN, A, B)

    def alter_row(self, board, prefix, change):
        path = self.root / (board + '.ros.log')
        lines = path.read_text().splitlines()
        changed = False
        for i, line in enumerate(lines):
            if line.startswith(prefix):
                value = json.loads(line[len(prefix):])
                change(value)
                lines[i] = prefix + json.dumps(value)
                changed = True
                break
        self.assertTrue(changed)
        path.write_text('\n'.join(lines) + '\n')

    def test_original_passes_only_the_narrow_gate(self):
        result = self.result()
        self.assertTrue(result['passed'], result['errors'])
        self.assertFalse(result['full_graph_gate'])
        self.assertFalse(result['full_cli_gate'])

    def test_failed_real_child_cannot_be_hidden_by_pass_text(self):
        p = self.root / (A + '.ros.status.json')
        v = json.loads(p.read_text())
        v['returncode'] = 1
        p.write_text(json.dumps(v))
        self.assertFalse(self.result()['passed'])

    def test_payload_corruption_fails_despite_matching_counts(self):
        self.alter_row(A, 'ROS_BROKER_PHASE ', lambda v: v['received']['alpha'].__setitem__(0, 'incorrect payload'))
        self.assertFalse(self.result()['passed'])

    def test_duplicate_ghost_node_is_rejected(self):
        self.alter_row(B, 'ROS_BROKER_PHASE ', lambda v: v['nodes'].append(v['nodes'][0]))
        self.assertFalse(self.result()['passed'])

    def test_missing_peer_withdrawal_is_rejected(self):
        p = self.root / (B + '.ros.log')
        p.write_text('\n'.join((x for x in p.read_text().splitlines() if not x.startswith('ROS_BROKER_PEER_WITHDRAWN '))) + '\n')
        self.assertFalse(self.result()['passed'])

    def test_wrong_enclave_association_is_rejected(self):
        self.alter_row(A, 'ROS_BROKER_PHASE ', lambda v: v['enclaves'][0].__setitem__(2, '/wrong_context'))
        self.assertFalse(self.result()['passed'])

    def test_wrong_loaded_library_is_rejected(self):
        self.alter_row(A, 'ROS_BROKER_PROVENANCE ', lambda v: v.update(libmdds_paths=['/data/local/tmp/ros2/lib/libmdds.so']))
        self.assertFalse(self.result()['passed'])

    def test_owned_udp_is_rejected(self):
        p = self.root / (A + '.daemon.inspect.json')
        v = json.loads(p.read_text())
        v['owned_udp'] = ['owned socket']
        p.write_text(json.dumps(v))
        self.assertFalse(self.result()['passed'])

    def test_nonzero_broker_resources_are_rejected(self):
        p = self.root / (B + '.daemon.log')
        text = p.read_text()
        self.assertIn('pending_retirements=0', text)
        p.write_text(text.replace('pending_retirements=0', 'pending_retirements=1'))
        self.assertFalse(self.result()['passed'])
if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]], verbosity=2)
