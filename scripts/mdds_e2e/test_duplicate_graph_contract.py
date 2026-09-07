"""Independent expectations for same-name nodes in distinct participants."""
import unittest
from duplicate_graph_contract import validate, topic, namespace


def sample(phase):
    indices=(0,1) if phase==1 else (0,)
    keys=[r+str(i) for r in ('A','B') for i in indices]
    topics={topic('run',k):['std_msgs/msg/String'] for k in keys}
    services={topic('run',k)+'/serve':['example_interfaces/srv/AddTwoInts'] for k in keys}
    owned_services={**services,namespace('run')+'/same/get_type_description':['type_description_interfaces/srv/GetTypeDescription']}
    return {'phase':phase,'nodes':[['same',namespace('run')]]*len(keys),
            'publishers':topics,'subscriptions':topics,'services':owned_services,'clients':services,
            'gids':{k:[ord(k[0]),int(k[1])+1]+[0]*10+[1,0,0,2] for k in keys}}


class Contract(unittest.TestCase):
    def test_exact_four_then_two(self):
        first=sample(1);validate(first,'run',1);validate(sample(2),'run',2,first)
    def test_collapsed_node_multiplicity_rejected(self):
        value=sample(1);value['nodes']=value['nodes'][:1]
        with self.assertRaises(ValueError):validate(value,'run',1)
    def test_shared_participant_rejected(self):
        value=sample(1);value['gids']['A1'][:12]=value['gids']['A0'][:12]
        with self.assertRaises(ValueError):validate(value,'run',1)
    def test_stale_endpoint_rejected(self):
        value=sample(2);value['publishers'].update(sample(1)['publishers'])
        with self.assertRaises(ValueError):validate(value,'run',2,sample(1))
    def test_survivor_gid_replacement_rejected(self):
        value=sample(2);value['gids']['B0'][3]=55
        with self.assertRaises(ValueError):validate(value,'run',2,sample(1))
    def test_missing_owner_service_rejected(self):
        value=sample(1);value['services'].pop(next(iter(value['services'])))
        with self.assertRaises(ValueError):validate(value,'run',1)


if __name__=='__main__':unittest.main()
