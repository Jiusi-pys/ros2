"""Churn requires every phase and exact stable/removed ownership."""
import unittest
from churn_graph_contract import ROUNDS,scope,expected,validate_snapshot,validate_history


def snapshot(index):
    value=expected('run',index)
    value['gids']={role+':survivor':[ord(role)]+[0]*11+[1,0,0,4] for role in ('A','B')}
    if index%2==0:
        for role in ('A','B'):value['gids'][role+':transient']=[ord(role),index+1]+[0]*10+[1,0,0,4]
    return value


class ChurnContract(unittest.TestCase):
    def test_twenty_complete_create_remove_rounds(self):
        self.assertEqual(ROUNDS,20)
        validate_history([snapshot(i) for i in range(40)],'run')
    def test_complete_entity_sets(self):
        active=expected('run',0);removed=expected('run',1);ns=scope('run')
        self.assertEqual(active['nodes'],sorted([ns+'/survivor_A',ns+'/survivor_B',ns+'/A/transient',ns+'/B/transient']))
        self.assertEqual(removed['nodes'],[ns+'/survivor_A',ns+'/survivor_B'])
        self.assertEqual((len(active['topics']),len(removed['topics'])),(10,4))
        self.assertEqual((len(active['services']),len(removed['services'])),(14,4))
        self.assertEqual(active['by_node'][ns+'/A/transient']['action_servers'],{ns+'/A/transient/action':['example_interfaces/action/Fibonacci']})
    def test_stale_endpoint_after_removal_rejected(self):
        value=snapshot(1);value['topics'][scope('run')+'/A/transient/out']=['std_msgs/msg/String']
        with self.assertRaises(ValueError):validate_snapshot(value,'run',1)
    def test_survivor_gid_must_not_change(self):
        values=[snapshot(i) for i in range(40)];values[17]['gids']['A:survivor'][3]=7
        with self.assertRaises(ValueError):validate_history(values,'run')
    def test_transient_gid_must_not_be_reused(self):
        values=[snapshot(i) for i in range(40)];values[2]['gids']['B:transient']=values[0]['gids']['B:transient']
        with self.assertRaises(ValueError):validate_history(values,'run')
    def test_phase_omission_rejected(self):
        with self.assertRaises(ValueError):validate_history([snapshot(i) for i in range(39)],'run')
    def test_parameter_event_ghost_is_not_hidden_by_unscoped_topic(self):
        value=snapshot(1);value['parameter_event_owners'].append(scope('run')+'/A/transient')
        with self.assertRaises(ValueError):validate_snapshot(value,'run',1)


if __name__=='__main__':unittest.main()
