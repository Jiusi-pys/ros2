"""Remote graph changes must come from the peer, in exact phase order."""
import json
PHASES=[kind+'_'+operation for kind in ('publisher','subscription','service','client','node') for operation in ('create','destroy')]


def change_record(run,nonce,source,index):
    if source not in ('A','B') or type(index) is not int or not 0<=index<len(PHASES):raise ValueError('invalid remote phase')
    value={'run_id':run,'nonce':nonce,'source_role':source,'observer_role':'B' if source=='A' else 'A',
           'index':index,'phase':PHASES[index],'api_complete':True}
    if PHASES[index].startswith('node_'):value['bare_node']=True
    return value


def validate_change(value,run,nonce,observer,index):
    expected=change_record(run,nonce,'B' if observer=='A' else 'A',index)
    if json.dumps(value,sort_keys=True)!=json.dumps(expected,sort_keys=True):raise ValueError('remote graph source/phase differs')
