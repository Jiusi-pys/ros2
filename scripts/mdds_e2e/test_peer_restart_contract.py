"""Restart requires fresh process and endpoint identities, not just the same names."""
import copy
import unittest
from peer_restart_contract import scope,specs,validate_snapshot,validate_recovery,validate_processes

def snapshot(generation,roles=('A','B')):
    rows=[]
    for role in roles:
        for i,(topic,kind,type_) in enumerate(specs('run',role)):
            rows.append({'node':'peer_'+role,'namespace':scope('run'),'topic':topic,'kind':kind,'type':type_,'hash':'hash','qos':{'depth':10},'gid':[ord(role),generation]+[0]*10+[i,0,0,kind]})
    rows.sort(key=lambda v:tuple(v['gid']))
    return {'nodes':[['peer_'+r,scope('run'),'/peer/'+r+'/g'+str(generation)] for r in roles],'endpoints':rows,'catalog':sorted({v['topic'] for v in rows})}

class RestartContract(unittest.TestCase):
    def test_exact_rich_nodes_and_fresh_recovery(self):
        self.assertEqual(len(specs('run','A')),25)
        validate_snapshot(snapshot(1),'run',1,('A','B'))
        validate_recovery(snapshot(1),snapshot(2),'run')
    def test_same_gid_is_not_restart(self):
        value=snapshot(2);value['endpoints'][0]['gid']=snapshot(1)['endpoints'][0]['gid']
        with self.assertRaises(ValueError):validate_recovery(snapshot(1),value,'run')
    def test_missing_action_endpoint_rejected(self):
        value=snapshot(2);value['endpoints'].pop()
        with self.assertRaises(ValueError):validate_recovery(snapshot(1),value,'run')
    def test_changed_qos_after_restart_rejected(self):
        value=snapshot(2);value['endpoints'][0]['qos']['depth']=9
        with self.assertRaises(ValueError):validate_recovery(snapshot(1),value,'run')
    def test_process_tuple_and_terminal_are_required(self):
        first={'pid':10,'start':'100','returncode':-9};second={'pid':11,'start':'101','returncode':0}
        validate_processes(first,second)
        for a,b in [(dict(first,returncode=0),second),(first,dict(second,returncode=1)),(first,dict(second,pid=10,start='100'))]:
            with self.assertRaises(ValueError):validate_processes(a,b)

if __name__=='__main__':unittest.main()
