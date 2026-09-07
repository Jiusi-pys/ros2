"""Verify exact cross-participant graph snapshots and both data phases."""
import json
from duplicate_graph_contract import validate as validate_snapshot,payload
import cli_acceptance as a


def validate(value,root,run,board,nonce):
    role='A' if board==a.TARGET['board_serials'][0] else 'B';peer='B' if role=='A' else 'A'
    phases=value['duplicate_graph']
    if len(phases)!=2:raise ValueError('duplicate graph phases incomplete')
    raw=(root/(board+'.ros.log')).read_text().splitlines()
    for name in ('hidden_source.go','hidden_cli.go','hidden_source.stop','hidden_source.done','hidden_cli.ready','hidden_cli.done'):
        if (root/(board+'.'+name)).read_text().strip()!=nonce:raise ValueError('duplicate graph barrier differs')
    initial=None
    for phase,(record,name) in enumerate(zip(phases,('hidden_source.json','duplicate_survivor.json')),1):
        if any(record.get(k)!=v for k,v in {'run_id':run,'nonce':nonce,'role':role}.items()):raise ValueError('duplicate graph phase identity differs')
        if json.loads((root/(board+'.'+name)).read_bytes())!=record:raise ValueError('duplicate source report differs')
        if raw.count('DUPLICATE_GRAPH_PHASE '+json.dumps(record))!=1:raise ValueError('duplicate graph native snapshot missing')
        validate_snapshot(record['snapshot'],run,phase,initial)
        if phase==1:initial=record['snapshot']
        indices=(0,1) if phase==1 else (0,)
        if set(record['data'])!={role+str(i) for i in indices}:raise ValueError('duplicate data owners differ')
        for i in indices:
            key=role+str(i);data=record['data'][key];n=int(nonce[:7],16)
            expected={'received':[payload(run,nonce,peer+str(i),phase,j) for j in range(3)],'service_sum':n+100+phase,'served':[101] if phase==1 else [101,102]}
            if data!=expected:raise ValueError('duplicate peer payload/RPC differs')
            served={'key':key,'nonce':nonce,'a':n,'b':100+phase,'sum':n+100+phase}
            if raw.count('DUPLICATE_SERVICE_RX '+json.dumps(served))!=1:raise ValueError('duplicate server raw evidence missing')
