"""A stopped SDK driver and fresh restoration must preserve local ownership."""
import copy
import unittest
from remote_cycle_contract import validate

def records():
    common={'run_id':'run','nonce':'a'*32,'pid':123,'connections':2,'active_ports':2}
    values={p:{**common,'phase':p,'generation':g,'now_ms':t,'channels':c,'remote_links':c} for p,g,t,c in [('ready',1,10,1),('paused',1,20,0),('restored',2,30,1)]}
    for g in (1,2):values['stop'+str(g)]={**common,'generation':g,'channels':0,'pending_retirements':0,'queued_bytes':0,'reassembly_bytes':0}
    return values

class CycleContract(unittest.TestCase):
    def test_complete_cycle(self):validate(records(),'run','a'*32,123)
    def test_missing_shutdown_or_false_restore_rejected(self):
        for key,field,value in [('stop1','channels',1),('paused','remote_links',1),('restored','generation',1),('restored','channels',0)]:
            v=records();v[key][field]=value
            with self.assertRaises(ValueError):validate(v,'run','a'*32,123)
    def test_local_connection_loss_rejected(self):
        v=records();v['paused']['connections']=0
        with self.assertRaises(ValueError):validate(v,'run','a'*32,123)
    def test_wrong_owner_or_phase_order_rejected(self):
        for key,field,value in [('paused','nonce','old'),('restored','pid',456),('restored','now_ms',15)]:
            v=records();v[key][field]=value
            with self.assertRaises(ValueError):validate(v,'run','a'*32,123)

if __name__=='__main__':unittest.main()
