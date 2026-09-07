"""Validate expected SIGKILL, bounded withdrawal and unaffected survivor traffic."""
import hashlib
import json
import cli_acceptance as a
from abrupt_graph_contract import messages,validate_pair,validate_kill
from abrupt_victim_owner import owned_victim

def check_data(value,run,nonce,role,kind,stage):
    peer='B' if role=='A' else 'A';seed=int(nonce[:7],16)+(1 if role=='A' else 2)
    received={'survivor':messages(run,nonce,peer,'survivor',1)}
    if kind=='survivor':
        received['victim']=messages(run,nonce,peer,'victim',1)
        if stage==2:received['survivor']+=messages(run,nonce,peer,'survivor',2)
        served=[1111,2222]+([3333] if stage==2 else [])
        results={'survivor':seed+1111,'victim':seed+1112} if stage==1 else {'survivor':seed+3333}
    else:served=[1112];results={'survivor':seed+2222}
    if value!={'received':received,'served':served,'results':results}:raise ValueError('abrupt peer data/RPC result differs')

def validate(value,root,run,board,nonce):
    role='A' if board==a.TARGET['board_serials'][0] else 'B';peer='B' if role=='A' else 'A';peer_board=next(b for b in a.TARGET['board_serials'] if b!=board)
    report=value['abrupt_graph'];remote='/data/local/tmp/ros2/.mdds-owned-runs/'+run
    if any(report.get(k)!=v for k,v in {'run_id':run,'nonce':nonce,'role':role}.items()):raise ValueError('abrupt identity differs')
    if json.loads((root/(board+'.abrupt_graph.json')).read_bytes())!=report:raise ValueError('abrupt native report differs')
    raw=(root/(board+'.ros.log')).read_text().splitlines()
    if raw.count('ABRUPT_GRAPH_RESULT '+json.dumps(report))!=1:raise ValueError('abrupt final native record missing')
    before,after=report['before'],report['after'];validate_pair(before['snapshot'],after['snapshot'],run)
    other=json.loads((root/(peer_board+'.abrupt_graph.json')).read_bytes())
    for key,stage in (('before',1),('after',2)):
        record=report[key]
        if json.dumps(record['snapshot'],sort_keys=True)!=json.dumps(other[key]['snapshot'],sort_keys=True):raise ValueError('abrupt peer snapshots disagree')
        if json.loads((root/(board+'.abrupt_'+key+'.json')).read_bytes())!=record or raw.count('ABRUPT_'+key.upper()+' '+json.dumps(record))!=1:raise ValueError('abrupt phase lacks raw evidence')
        check_data(record['data'],run,nonce,role,'survivor',stage)
    if after['ready']!={'survivor':True,'victim':False} or report['final']!={'nodes':[],'topics':{},'services':{},'parameter_event_owners':[]}:raise ValueError('dead service readiness or final cleanup differs')
    arm=json.loads((root/(board+'.abrupt_armed.json')).read_bytes());kill=json.loads((root/(board+'.victim.status.json')).read_bytes())
    if any(arm.get(k)!=v for k,v in {'run_id':run,'nonce':nonce,'role':role,'armed_ns':after['armed_ns']}.items()) or raw.count('ABRUPT_ARM '+json.dumps(arm))!=1:raise ValueError('abrupt arm differs')
    validate_kill(kill,arm['armed_ns'],after['removed_ns'])
    peer_arm=(root/(board+'.peer_armed.json')).read_bytes()
    if peer_arm!=(root/(peer_board+'.abrupt_armed.json')).read_bytes() or hashlib.sha256(peer_arm).hexdigest()!=kill['peer_armed_sha256']:raise ValueError('peer was not causally armed before kill')
    argv=['/data/python312-rk3588a/usr/bin/python3.12','-u','-B',remote+'/board_abrupt_victim.py',remote,run,role,nonce]
    if any(kill.get(k)!=v for k,v in {'run_id':run,'nonce':nonce,'role':role,'argv':argv}.items()) or not owned_victim(kill['identity'],remote,argv,kill['pid'],kill['start']):raise ValueError('killed process was not the owned victim')
    victim=json.loads((root/(board+'.victim.ready.json')).read_bytes());victim_raw=(root/(board+'.victim.log')).read_text().splitlines()
    if any(victim.get(k)!=v for k,v in {'run_id':run,'nonce':nonce,'role':role,'pid':kill['pid'],'start':kill['start'],'gid':before['snapshot']['gids'][role+':victim']}.items()):raise ValueError('victim ready identity differs')
    check_data(victim['data'],run,nonce,role,'victim',1)
    provenance=victim['provenance']
    if provenance['pid']!=kill['pid'] or provenance['libmdds_paths']!=[remote+'/lib/libmdds.so'] or provenance['librmw_mdds_paths']!=[remote+'/lib/librmw_mdds.so'] or provenance['owned_udp_sockets']:raise ValueError('victim native transport provenance differs')
    package=json.loads((root/'rclpy_package.json').read_bytes())
    if provenance['rclpy']['native_sha256']!=package['native_sha256']:raise ValueError('victim rclpy differs')
    for marker in ('MDDS_GRAPH_ACTUAL_ARGV '+json.dumps(argv),'ABRUPT_VICTIM_READY '+json.dumps(victim),'ABRUPT_KILL '+json.dumps(kill),a.terminal_marker(run,'graph:abrupt_exit',-9,argv,board)):
        if victim_raw.count(marker)!=1:raise ValueError('victim argv/ready/kill/terminal evidence missing')
    for kind,data,lines in (('survivor',after['data'],raw),('victim',victim['data'],victim_raw)):
        for source,items in data['received'].items():
            for message in items:
                marker={'run_id':run,'nonce':nonce,'role':role,'owner':kind,'source':source,'data':message}
                if lines.count('ABRUPT_RX '+json.dumps(marker))!=1:raise ValueError('abrupt receiving callback missing or duplicated')
        for b in data['served']:
            operand=int(nonce[:7],16)+(1 if peer=='A' else 2)
            marker={'run_id':run,'nonce':nonce,'role':role,'owner':kind,'a':operand,'b':b,'sum':operand+b}
            if lines.count('ABRUPT_SERVICE_RX '+json.dumps(marker))!=1:raise ValueError('abrupt service callback missing or duplicated')
    for name in ('hidden_source.go','hidden_cli.go','hidden_cli.ready','hidden_cli.done','hidden_source.stop','hidden_source.done','victim_kill.go','abrupt_after.ready'):
        if (root/(board+'.'+name)).read_text().strip()!=nonce:raise ValueError('abrupt lifecycle barrier differs')
