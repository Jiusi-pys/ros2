"""Validate a CLI-created native child, peer data and graceful disappearance."""
import json
import re
import cli_acceptance as a
from cli_standalone import native_matches


def validate(execution,expected,raw,root,run,board,nonce):
    for peer_board in a.TARGET['board_serials']:
        for stage in ('ready','start','stop'):
            if (root/(peer_board+'.standalone.'+stage)).read_text().strip()!=nonce:raise ValueError('standalone barrier evidence differs')
    detail=execution.get('standalone',{});native=detail.get('native',{})
    remote='/data/local/tmp/ros2/.mdds-owned-runs/'+run
    if not native_matches(native,remote,execution['child_pid'],expected) or native.get('run_id')!=run or native.get('owned_udp')!=[]:raise ValueError('standalone native identity differs')
    hashes={remote+'/lib/'+name:a.digest((root/name).read_bytes()) for name in ('libmdds.so','librmw_mdds.so')}
    hashes[remote+'/component_prefix/lib/rclcpp_components/component_container']=a.digest((root/'component_container').read_bytes())
    hashes[remote+'/component_prefix/lib/libtalker_component.so']=a.digest((root/'libtalker_component.so').read_bytes())
    if native['hashes']!=hashes:raise ValueError('standalone native libraries differ')
    shutdown={'signal':2,'cli_pid':execution['child_pid'],'cli_start':execution['child_start'],'container_pid':native['pid'],'container_start':native['start'],'barrier_nonce':nonce}
    if detail.get('shutdown')!=shutdown or detail.get('container_gone') is not True or detail.get('emergency_cleanup') is not False or execution['returncode']!=0:raise ValueError('standalone did not stop cleanly')
    if raw.splitlines().count('MDDS_STANDALONE_PROCESS '+json.dumps(detail))!=1:raise ValueError('standalone process evidence missing from log')
    peer_board=next(b for b in a.TARGET['board_serials'] if b!=board)
    received=json.loads((root/(peer_board+'.standalone_received.json')).read_text())
    gone=json.loads((root/(peer_board+'.standalone_gone.json')).read_text())
    for stage,proof in [('received',received),('gone',gone)]:
        identity={'run_id':run,'nonce':nonce,'board':peer_board,'peer_role':expected['role'],'stage':stage}
        if any(proof.get(k)!=v for k,v in identity.items()):raise ValueError('standalone observation identity differs')
        if (root/(peer_board+'.ros.log')).read_text().splitlines().count('CLI_STANDALONE_PROOF '+json.dumps(proof))!=1:raise ValueError('standalone observer log missing')
    endpoint=received['endpoint'];gid=endpoint.get('gid',[])
    type_hash=json.loads((root/'type_hashes.json').read_text())['hashes']['std_msgs/msg/String']
    if endpoint!={'node':expected['node'],'namespace':expected['namespace'],'type':'std_msgs/msg/String','type_hash':type_hash,'gid':gid} or len(gid)!=16 or not any(gid) or any(type(b) is not int or not 0<=b<=255 for b in gid):raise ValueError('standalone endpoint metadata differs')
    messages=received['received']
    if not 2<=len(messages)<=120:raise ValueError('standalone peer data missing')
    numbers=[]
    for message in messages:
        match=re.fullmatch('Hello World: ([1-9][0-9]*)',message)
        if not match or expected['node']+"]: Publishing: '"+message+"'" not in raw:raise ValueError('standalone payload lacks actual publisher log')
        numbers.append(int(match[1]))
    if numbers!=list(range(numbers[0],numbers[0]+len(numbers))):raise ValueError('standalone payload order differs')
    if any(gone.get(k) is not True for k in ('component_absent','container_absent','publisher_absent')):raise ValueError('standalone graph did not withdraw')
