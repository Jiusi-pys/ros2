"""Remote mutation identity and phase ordering are part of guard acceptance."""
from pathlib import Path
import unittest


class RemoteGraphContractTest(unittest.TestCase):
    def test_node_phase_is_not_an_endpoint_side_effect(self):
        from remote_graph_contract import change_record
        self.assertTrue(change_record('run','a'*32,'B',8)['bare_node'])
    def test_distinct_remote_native_target(self):
        source=(Path(__file__).parent/'graph_waiters/CMakeLists.txt').read_text()
        self.assertIn('graph_remote_waiters',source)
        self.assertIn('GRAPH_REMOTE_WAITER=1',source)
    def test_source_record_requires_opposite_board(self):
        from remote_graph_contract import change_record,validate_change
        value=change_record('run','a'*32,'B',0)
        validate_change(value,'run','a'*32,'A',0)
        value['source_role']='A'
        with self.assertRaises(ValueError):validate_change(value,'run','a'*32,'A',0)
    def test_old_phase_cannot_satisfy_new_wait(self):
        from remote_graph_contract import change_record,validate_change
        with self.assertRaises(ValueError):validate_change(change_record('run','a'*32,'B',0),'run','a'*32,'A',1)
    def test_stale_run_cannot_satisfy_wait(self):
        from remote_graph_contract import change_record,validate_change
        with self.assertRaises(ValueError):validate_change(change_record('old','b'*32,'B',0),'run','a'*32,'A',0)


if __name__=='__main__':unittest.main()
