"""Bind peer generation replacement to native process, graph, message and RPC evidence."""
import json
from cli_acceptance import TARGET,digest
from abrupt_victim_owner import owned_victim
from peer_restart_contract import scope,path,payloads,validate_snapshot,validate_recovery,validate_processes

def partition(value,role):
    rows=[v for v in value['endpoints'] if v['node']=='peer_'+role]
    return {'nodes':[v for v in value['nodes'] if v[0]=='peer_'+role],'endpoints':rows,'catalog':sorted({v['topic'] for v in rows})}

def check(root):
    run=root.name;nonce=(root/'nonce').read_text().strip();remote='/data/local/tmp/ros2/.mdds-owned-runs/'+run
    hashes=json.loads((root/'late_graph_hashes.json').read_bytes())['hashes'];reports={};result={}
    for board,role in zip(TARGET['board_serials'],('A','B')):
        peer='B' if role=='A' else 'A';raw=(root/(board+'.ros.log')).read_text().splitlines()
        phases={stage:json.loads((root/(board+'.reconnect.peer_'+stage+'.json')).read_bytes()) for stage in ('before','paused','old_gone','new_local','restored','final')}
        for stage,generation,roles in [('before',1,('A','B')),('paused',1,(role,)),('old_gone',1,()),('new_local',2,(role,)),('restored',2,('A','B')),('final',2,())]:
            v=phases[stage]
            if any(v.get(k)!=expected for k,expected in {'run_id':run,'nonce':nonce,'role':role}.items()) or raw.count('PEER_GRAPH_'+stage.upper()+' '+json.dumps(v))!=1:raise ValueError('peer graph phase identity/raw record differs')
            validate_snapshot(v['snapshot'],run,generation,roles)
            if any(e['hash']!=hashes[e['type']] for e in v['snapshot']['endpoints']):raise ValueError('peer type hash differs')
        positions=[raw.index('PEER_GRAPH_'+stage.upper()+' '+json.dumps(phases[stage])) for stage in ('before','paused','old_gone','new_local','restored','final')]
        if positions!=sorted(positions):raise ValueError('peer graph phases are out of order')
        before,after=phases['before']['snapshot'],phases['restored']['snapshot'];validate_recovery(before,after,run)
        if phases['paused']['snapshot']!=partition(before,role) or phases['new_local']['snapshot']!=partition(after,role):raise ValueError('local peer graph changed across a stable stage')
        statuses={g:json.loads((root/(board+f'.reconnect.peer_{g}.status.json')).read_bytes()) for g in (1,2)}
        validate_processes(statuses[1],statuses[2])
        worker=json.loads((root/(board+'.peer_worker.status.json')).read_bytes())
        if worker['returncode']!=0 or worker['run_id']!=run or worker['role']!='peer_worker':raise ValueError('peer owner worker did not complete')
        expected_owner=f"MDDS_OWNED_PROCESS RUN_ID={run} TAG=peer_worker_child PID={worker['child_pid']} START={worker['child_start']}"
        initial=(root/(board+'.peer_worker.child.pid')).read_text().strip()
        final_path=root/(board+'.peer_worker.child.final.pid')
        if initial!=expected_owner or (not final_path.exists() or final_path.read_text().strip()!=initial):raise ValueError('peer owner worker changed')
        if (root/(board+'.peer_worker.log')).read_text().splitlines().count('GRAPH_PROCESS_EXIT '+json.dumps(worker,sort_keys=True))!=1:raise ValueError('peer worker native exit missing')
        for generation in (1,2):
            status=statuses[generation];argv=['/data/python312-rk3588a/usr/bin/python3.12','-u','-B',remote+'/board_restart_peer.py',remote,run,role,nonce,str(generation)]
            if any(status.get(k)!=v for k,v in {'run_id':run,'nonce':nonce,'role':role,'generation':generation,'argv':argv}.items()):raise ValueError('peer process generation/argv differs')
            lines=(root/(board+f'.peer_{generation}.log')).read_text().splitlines()
            created=json.loads((root/(board+f'.reconnect.peer_{generation}.created.json')).read_bytes());ready=json.loads((root/(board+f'.reconnect.peer_{generation}.ready.json')).read_bytes())
            for v in (created,ready):
                if any(v.get(k)!=expected for k,expected in {'run_id':run,'nonce':nonce,'role':role,'generation':generation,'pid':status['pid'],'start':status['start']}.items()):raise ValueError('peer creation identity differs')
            snapshot=before if generation==1 else after
            gid=next(e['gid'] for e in snapshot['endpoints'] if e['node']=='peer_'+role and e['topic']=='rt'+path(run,role)+'/out' and e['kind']==1)
            if created['gid']!=gid or ready['gid']!=gid:raise ValueError('peer process does not own observed GID')
            prov=created['provenance'];package=json.loads((root/'rclpy_package.json').read_bytes())
            if prov['pid']!=status['pid'] or prov['owned_udp_sockets'] or prov['libmdds_paths']!=[remote+'/lib/libmdds.so'] or prov['librmw_mdds_paths']!=[remote+'/lib/librmw_mdds.so'] or prov['rclpy']['native_sha256']!=package['native_sha256']:raise ValueError('peer native provenance differs')
            if ready['provenance']!=prov or ready['received']!=payloads(run,nonce,peer,generation) or ready['rpc_sum']!=int(nonce[:7],16)+(1 if role=='A' else 2)+7000+generation:raise ValueError('peer new-generation traffic/RPC differs')
            operand=int(nonce[:7],16)+(1 if peer=='A' else 2)
            server={'run_id':run,'nonce':nonce,'role':role,'generation':generation,'a':operand,'b':7000+generation,'sum':operand+7000+generation}
            if ready['served']!=[server] or lines.count('RESTART_PEER_SERVER '+json.dumps(server))!=1:raise ValueError('peer server callback differs')
            for message in ready['received']:
                marker={'run_id':run,'nonce':nonce,'role':role,'generation':generation,'data':message}
                if lines.count('RESTART_PEER_RX '+json.dumps(marker))!=1:raise ValueError('peer payload callback missing or duplicated')
            positions=[lines.index(marker) if marker in lines else -1 for marker in ('RESTART_PEER_CREATED '+json.dumps(created),'RESTART_PEER_READY '+json.dumps(ready),'RESTART_PEER_EXIT '+json.dumps(status))]
            if min(positions)<0 or positions!=sorted(positions):raise ValueError('peer lifecycle phases out of order')
            for marker in ('RESTART_PEER_ARGV '+json.dumps(argv),'RESTART_PEER_CREATED '+json.dumps(created),'RESTART_PEER_READY '+json.dumps(ready),'RESTART_PEER_EXIT '+json.dumps(status)):
                if lines.count(marker)!=1:raise ValueError('peer native lifecycle record missing')
        killed=json.loads((root/(board+'.peer_kill.json')).read_bytes());first=statuses[1]
        if any(killed.get(k)!=v for k,v in {'run_id':run,'nonce':nonce,'role':role,'signal':9}.items()) or not owned_victim(killed['identity'],remote,first['argv'],first['pid'],first['start']):raise ValueError('peer kill was not owned')
        for name in ('peer_restart.enabled','peer_restart.go','peer_stop.go'):
            if (root/(board+'.'+name)).read_text().strip()!=nonce:raise ValueError('peer restart barrier differs')
        reports[role]=phases;result[role]={'before_endpoints':len(before['endpoints']),'after_endpoints':len(after['endpoints']),'old_process':statuses[1],'new_process':statuses[2]}
    for stage in ('before','restored','final'):
        if reports['A'][stage]['snapshot']!=reports['B'][stage]['snapshot']:raise ValueError('peer observers disagree')
    return result

def validate_complete(value,root,run,board,nonce):
    from verify_remote_cycle import check as check_all
    for flag in ('peer_restart.enabled','cycle_graph.enabled','reconnect.enabled'):
        if (root/flag).read_text().strip()!=nonce:raise ValueError('reconnection feature declaration missing')
    result=check_all(root)
    if 'graph_cycle' not in result or 'peer_restart' not in result:raise ValueError('full reconnection checks disabled')
    final=json.loads((root/(board+'.reconnect.peer_final.json')).read_bytes())
    if value['peer_restart']!=final:raise ValueError('final peer proof differs')
