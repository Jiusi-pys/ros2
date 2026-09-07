"""Bind disconnected/restored graph snapshots to raw ROS traffic and peer RPC."""
import json
from cycle_graph_contract import validate_views
from cli_acceptance import TARGET

def check(root):
    run=root.name;nonce=(root/'nonce').read_text().strip();records={};result={}
    for board,role in zip(TARGET['board_serials'],('A','B')):
        raw=(root/(board+'.ros.log')).read_text().splitlines()
        phases={stage:json.loads((root/(board+'.reconnect.graph_'+stage+'.json')).read_bytes()) for stage in ('ready','paused','restored','done')}
        for stage,v in phases.items():
            if any(v.get(k)!=expected for k,expected in {'run_id':run,'nonce':nonce,'role':role}.items()):raise ValueError('cycle graph phase identity differs')
            if raw.count('CYCLE_GRAPH_'+stage.upper()+' '+json.dumps(v))!=1:raise ValueError('cycle graph phase lacks raw evidence')
        validate_views(phases['ready']['snapshot'],phases['paused']['snapshot'],phases['restored']['snapshot'],role)
        expected=[f'{run}|{nonce}|{role}|local|{i}' for i in range(3)]
        if phases['paused']['local_received']!=expected or phases['paused']['remote_service_ready'] is not False or phases['paused']['remote_subscriptions']!=0:raise ValueError('local traffic or remote readiness during outage differs')
        for message in expected:
            if raw.count('CYCLE_LOCAL_RX '+json.dumps({'run_id':run,'nonce':nonce,'role':role,'data':message}))!=1:raise ValueError('outage local receiving callback missing')
        restored=phases['restored'];operand=int(nonce[:7],16)+(1 if role=='A' else 2)
        if (restored['a'],restored['b'],restored['sum'])!=(operand,177717,operand+177717):raise ValueError('recovered RPC differs')
        if phases['done']!={'run_id':run,'nonce':nonce,'role':role,'local_probe_removed':True}:raise ValueError('local probe cleanup differs')
        for name in ('cycle_graph.enabled','cycle_graph.release'):
            if (root/(board+'.'+name)).read_text().strip()!=nonce:raise ValueError('cycle graph barrier differs')
        records[role]=phases
        result[role]={'before_nodes':len(phases['ready']['snapshot']['nodes']),'paused_nodes':len(phases['paused']['snapshot']['nodes']),
                      'before_endpoints':len(phases['ready']['snapshot']['endpoints']),'paused_endpoints':len(phases['paused']['snapshot']['endpoints'])}
    if records['A']['ready']['snapshot']!=records['B']['ready']['snapshot'] or records['A']['restored']['snapshot']!=records['B']['restored']['snapshot']:raise ValueError('peer endpoint snapshots disagree')
    for board,role in zip(TARGET['board_serials'],('A','B')):
        peer='B' if role=='A' else 'A';v=records[peer]['restored']
        server={'run_id':run,'nonce':nonce,'role':role,'a':v['a'],'b':v['b'],'sum':v['sum']}
        if json.loads((root/(board+'.cycle_rpc_server.json')).read_bytes())!=server or (root/(board+'.ros.log')).read_text().splitlines().count('CYCLE_RPC_SERVER '+json.dumps(server))!=1:raise ValueError('peer RPC server callback differs')
    return result
