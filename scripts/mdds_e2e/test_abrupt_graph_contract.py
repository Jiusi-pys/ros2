"""A dead server differs from a surviving client-only graph name."""
import unittest
from abrupt_graph_contract import expected,scope,base,validate_snapshot,validate_kill


class AbruptContract(unittest.TestCase):
    def test_live_and_removed_ownership(self):
        live=expected('run',True);dead=expected('run',False)
        self.assertEqual((len(live['nodes']),len(dead['nodes'])),(4,2))
        self.assertEqual((len(live['topics']),len(dead['topics'])),(8,4))
        self.assertEqual((len(live['services']),len(dead['services'])),(14,6))
        self.assertIn(base('run','B','victim')+'/serve',dead['services'])
        self.assertEqual(dead['counts']['B:victim'],{'publishers':0,'subscriptions':1,'servers':0,'clients':1})
    def test_dead_server_must_not_remain_matched(self):
        value=expected('run',False);value['counts']['A:victim']['servers']=1
        with self.assertRaises(ValueError):validate_snapshot(value,'run',False)
    def test_client_only_name_is_required(self):
        value=expected('run',False);value['services'].pop(base('run','B','victim')+'/serve')
        with self.assertRaises(ValueError):validate_snapshot(value,'run',False)
    def test_parameter_event_ghost_rejected(self):
        value=expected('run',False);value['parameter_event_owners'].append(scope('run')+'/victim_A')
        with self.assertRaises(ValueError):validate_snapshot(value,'run',False)
    def test_sigkill_and_causal_time_are_required(self):
        value={'returncode':-9,'signal':9,'started_ns':120,'completed_ns':130}
        validate_kill(value,100,140)
        for change in ({'returncode':0},{'signal':15},{'started_ns':90},{'completed_ns':150}):
            with self.assertRaises(ValueError):validate_kill({**value,**change},100,140)
    def test_removal_is_bounded(self):
        with self.assertRaises(ValueError):validate_kill({'returncode':-9,'signal':9,'started_ns':120,'completed_ns':130},100,6_000_000_100)

if __name__=='__main__':unittest.main()
