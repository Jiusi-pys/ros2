"""Late discovery must preserve distinct node ownership in one participant."""
import unittest


class LateGraphContractTest(unittest.TestCase):
    def snapshot(self):
        from late_graph_contract import sections,endpoint_specs,space
        value={'nodes':['one','two'],'sections':{n:sections('run','B',n) for n in ('one','two')},'endpoints':{}}
        hashes={};counter=0
        for name in ('one','two'):
            value['endpoints'][name]={}
            for key,spec in endpoint_specs('run','B',name).items():
                counter+=1;hashes[spec['type']]='RIHS01_'+'1'*64
                value['endpoints'][name][key]={'node':name,'namespace':space('run','B'),'type':spec['type'],'type_hash':hashes[spec['type']],
                    'direction':spec['direction'],'gid':[1]*12+[0,0,counter,1],
                    'qos':{'history':1,'depth':7 if name=='one' else 11,'reliability':1,'durability':2,'deadline':0,'lifespan':0,'liveliness':1,'lease':0}}
        return value,hashes
    def test_complete_endpoint_snapshot(self):
        from late_graph_contract import validate_snapshot
        value,hashes=self.snapshot();validate_snapshot(value,'run','B',hashes)
    def test_nodes_must_share_one_participant(self):
        from late_graph_contract import validate_snapshot
        value,hashes=self.snapshot();next(iter(value['endpoints']['two'].values()))['gid'][0]=2
        with self.assertRaises(ValueError):validate_snapshot(value,'run','B',hashes)
    def test_endpoint_gid_must_be_unique(self):
        from late_graph_contract import validate_snapshot
        value,hashes=self.snapshot();a,b=list(value['endpoints']['one'].values())[:2];b['gid']=a['gid']
        with self.assertRaises(ValueError):validate_snapshot(value,'run','B',hashes)
    def test_wrong_endpoint_owner_rejected(self):
        from late_graph_contract import validate_snapshot
        value,hashes=self.snapshot();next(iter(value['endpoints']['one'].values()))['node']='two'
        with self.assertRaises(ValueError):validate_snapshot(value,'run','B',hashes)
    def test_wrong_type_hash_rejected(self):
        from late_graph_contract import validate_snapshot
        value,hashes=self.snapshot();next(iter(value['endpoints']['one'].values()))['type_hash']='RIHS01_'+'0'*64
        with self.assertRaises(ValueError):validate_snapshot(value,'run','B',hashes)
    def test_two_nodes_have_distinct_application_endpoints(self):
        from late_graph_contract import sections
        one=sections('run','B','one');two=sections('run','B','two')
        for kind in ('services','clients','action_servers','action_clients','subscriptions'):
            self.assertTrue(one[kind]);self.assertTrue(two[kind]);self.assertFalse(set(one[kind])&set(two[kind]))
    def test_expected_pair_rejects_missing_node(self):
        from late_graph_contract import validate_sections,sections
        with self.assertRaises(ValueError):validate_sections({'one':sections('run','B','one')},'run','B')
    def test_endpoint_inheritance_is_rejected(self):
        from late_graph_contract import validate_sections,sections
        value={n:sections('run','B',n) for n in ('one','two')}
        value['one']['services'].update(value['two']['services'])
        with self.assertRaises(ValueError):validate_sections(value,'run','B')
    def test_complete_sections(self):
        from late_graph_contract import validate_sections,sections
        validate_sections({n:sections('run','B',n) for n in ('one','two')},'run','B')


if __name__=='__main__':unittest.main()
