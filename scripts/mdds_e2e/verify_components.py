"""Bind native component ownership, data and selective retirement evidence."""
import json
import re
import cli_acceptance as a


def validate(value,root,run,board,nonce):
    own='A' if board==a.TARGET['board_serials'][0] else 'B';peer='B' if own=='A' else 'A'
    peer_board=next(b for b in a.TARGET['board_serials'] if b!=board)
    remote='/data/local/tmp/ros2/.mdds-owned-runs/'+run;space='/components_'+run
    process=value['container'];exe=remote+'/component_prefix/lib/rclcpp_components/component_container'
    hashes={remote+'/lib/'+name:a.digest((root/name).read_bytes()) for name in ('libmdds.so','librmw_mdds.so')}
    hashes[exe]=a.digest((root/'component_container').read_bytes())
    hashes[remote+'/component_prefix/lib/libtalker_component.so']=a.digest((root/'libtalker_component.so').read_bytes())
    if process['run_id']!=run or process['executable']!=exe or process['hashes']!=hashes or process['owned_udp'] or process['argv']!=[exe,'--ros-args','-r','__node:=container_'+own,'-r','__ns:='+space]:raise ValueError('wrong native component process provenance')
    status=json.loads((root/(board+'.container.status.json')).read_text())
    record=(root/(board+'.container.child.pid')).read_text().strip()
    if status['run_id']!=run or status['role']!='container' or status['returncode']!=0 or status['child_pid']!=process['pid'] or status['child_start']!=process['start'] or record!=f"MDDS_OWNED_PROCESS RUN_ID={run} TAG=container_child PID={process['pid']} START={process['start']}":raise ValueError('container exit/ownership mismatch')
    proofs={stage:json.loads((root/(board+'.components_'+stage+'.json')).read_text()) for stage in ('loaded','retired','empty')}
    for stage,proof in proofs.items():
        identity={'run_id':run,'nonce':nonce,'board':board,'peer_role':peer,'stage':stage}
        if any(proof.get(k)!=v for k,v in identity.items()):raise ValueError('component observation identity mismatch')
        if (root/(board+'.ros.log')).read_text().splitlines().count('CLI_COMPONENT_PROOF '+json.dumps(proof))!=1:raise ValueError('component proof lacks raw observer log')
    loaded,retired,empty=(proofs[s] for s in ('loaded','retired','empty'))
    type_hash=json.loads((root/'type_hashes.json').read_text())['hashes']['std_msgs/msg/String']
    peer_log=(root/(peer_board+'.container.log')).read_text()
    for name in ('primary','survivor'):
        endpoint=loaded[name];gid=endpoint.get('gid',[])
        if endpoint!={'node':name+'_'+peer,'namespace':space,'type':'std_msgs/msg/String','type_hash':type_hash,'gid':gid} or len(gid)!=16 or not any(gid) or any(type(b) is not int or not 0<=b<=255 for b in gid):raise ValueError('component endpoint metadata mismatch')
        messages=loaded['received'][name]
        if not 2<=len(messages)<=120:raise ValueError('missing component payloads')
        numbers=[]
        for payload in messages:
            match=re.fullmatch('Hello World: ([1-9][0-9]*)',payload)
            if not match or name+'_'+peer+"]: Publishing: '"+payload+"'" not in peer_log:raise ValueError('received component payload lacks peer publication')
            numbers.append(int(match[1]))
        if numbers!=list(range(numbers[0],numbers[0]+len(numbers))):raise ValueError('component payload sequence differs')
    if loaded['primary']['gid']==loaded['survivor']['gid']:raise ValueError('components share writer GID')
    if retired.get('primary_absent') is not True or retired['survivor']!=loaded['survivor']:raise ValueError('unload did not preserve survivor endpoint')
    last=retired['survivor_after'];match=re.fullmatch('Hello World: ([1-9][0-9]*)',last)
    if not match or int(match[1])<=int(loaded['received']['survivor'][-1].split(': ')[1]) or 'survivor_'+peer+"]: Publishing: '"+last+"'" not in peer_log:raise ValueError('surviving component did not continue publishing')
    if any(empty.get(k) is not True for k in ('primary_absent','survivor_absent','container_visible')):raise ValueError('component cleanup graph differs')
