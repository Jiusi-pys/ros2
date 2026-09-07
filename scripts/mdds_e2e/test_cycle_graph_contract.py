"""A physical outage must remove remote ownership, not surviving local endpoints."""
import unittest
from cycle_graph_contract import local_view,validate_views

def before():
    return {'nodes':[['alpha_A','/n','/A/alpha'],['alpha_B','/n','/B/alpha']],
            'endpoints':[{'node':'alpha_A','topic':'rt/n/B/out','gid':[1]*16,'kind':2},{'node':'alpha_B','topic':'rt/n/B/out','gid':[2]*16,'kind':1}],
            'catalog':['rt/n/B/out']}

class CycleGraphContract(unittest.TestCase):
    def test_local_subscription_keeps_remote_named_topic(self):
        v=local_view(before(),'A');self.assertEqual(v['catalog'],['rt/n/B/out']);self.assertEqual(len(v['endpoints']),1)
        validate_views(before(),v,before(),'A')
    def test_remote_ghost_during_pause_rejected(self):
        with self.assertRaises(ValueError):validate_views(before(),before(),before(),'A')
    def test_local_identity_loss_rejected(self):
        v=local_view(before(),'A');v['endpoints'][0]['gid'][0]=9
        with self.assertRaises(ValueError):validate_views(before(),v,before(),'A')
    def test_restored_metadata_must_be_exact(self):
        after=before();after['endpoints'][1]['kind']=2
        with self.assertRaises(ValueError):validate_views(before(),local_view(before(),'A'),after,'A')
    def test_missing_catalog_is_not_empty_graph(self):
        v=local_view(before(),'A');v['catalog']=[]
        with self.assertRaises(ValueError):validate_views(before(),v,before(),'A')

if __name__=='__main__':unittest.main()
