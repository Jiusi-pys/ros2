"""Validate actual CLI-created native nodes and their peer graph lifecycle."""
import json
import re
import cli_acceptance as a
from cli_process import native_matches,native_exit_code


def validate(execution,expected,raw,root,run,board,nonce):
    for observed_board in a.TARGET['board_serials']:
        for stage in ('ready','start','stop'):
            if (root/(observed_board+'.process.'+stage)).read_text().strip()!=nonce:raise ValueError('process barrier differs')
    detail=execution['process'];native=detail['native'];remote='/data/local/tmp/ros2/.mdds-owned-runs/'+run
    if not native_matches(native,remote,execution['child_pid'],expected) or native.get('run_id')!=run or native.get('owned_udp')!=[]:raise ValueError('CLI native child differs')
    hashes={remote+'/lib/'+name:a.digest((root/name).read_bytes()) for name in ('libmdds.so','librmw_mdds.so','libtalker_library.so')}
    if (root/'librclcpp.so').is_file():hashes[remote+'/lib/librclcpp.so']=a.digest((root/'librclcpp.so').read_bytes())
    hashes[remote+'/execution_prefix/lib/demo_nodes_cpp/talker']=a.digest((root/'process_talker').read_bytes())
    if native['hashes']!=hashes:raise ValueError('CLI native binary hashes differ')
    shutdown={'signal':2,'cli_pid':execution['child_pid'],'cli_start':execution['child_start'],'native_pid':native['pid'],'native_start':native['start'],'barrier_nonce':nonce}
    if execution['argv'][1]=='test':
        shutdown.pop('signal');shutdown['method']='launch_testing_completion'
        from cli_test_report import validate_xml
        xml=root/(board+'.process_test.junit.xml');summary=validate_xml(xml.read_text())
        if detail.get('junit')!=summary or detail.get('junit_sha256')!=a.digest(xml.read_bytes()):raise ValueError('test JUnit report differs')
        config=json.loads((root/(board+'.process_test_config.json')).read_text())
        if config!={'run_id':run,'nonce':nonce,'board':board,'expected':expected}:raise ValueError('launch test configuration differs')
        for name in ('peer_talker_test.py','mdds_cli_fixture.package.xml','mdds_cli_fixture.marker'):
            if (root/('inputs_'+board+'.sha256')).read_text().splitlines().count(a.digest((root/name).read_bytes())+'  '+name)!=1:raise ValueError('installed test fixture input differs')
        assertions=json.loads((root/(board+'.process_test_assertions.json')).read_text())
        final={'run_id':run,'nonce':nonce,'board':board,'assertions':['native_publication','peer_exchange','native_exit'],'native_pid':native['pid'],'native_returncode':0}
        if assertions!=final or raw.splitlines().count('CLI_LAUNCH_TEST_ASSERTION '+json.dumps(final))!=1:raise ValueError('real launch test assertions missing')
        if detail.get('native_returncode')!=0 or native_exit_code(raw,native['pid'])!=0:raise ValueError('launch-tested native child failed')
    if execution['argv'][1]=='launch':
        shutdown['recipient']='cli'
        definition=root/'process_talker.launch.py';sha=a.digest(definition.read_bytes())
        if (root/('inputs_'+board+'.sha256')).read_text().splitlines().count(sha+'  process_talker.launch.py')!=1:raise ValueError('launch file differs from staged input')
        pid=str(native['pid'])
        if detail.get('native_returncode')!=0 or native_exit_code(raw,native['pid'])!=0 or raw.count('process started with pid ['+pid+']')!=1 or raw.count("sending signal 'SIGINT' to process[talker-1]")!=1:raise ValueError('launch did not manage native startup and shutdown')
    if detail['shutdown']!=shutdown or detail['native_gone'] is not True or detail['emergency_cleanup'] is not False or execution['returncode']!=0:raise ValueError('CLI native process did not stop cleanly')
    if raw.splitlines().count('MDDS_CLI_PROCESS '+json.dumps(detail))!=1 or 'dsoftbus(local=AF_UNIX physical=dsoftbus_broker' not in raw:raise ValueError('CLI process/transport log differs')
    peer=next(b for b in a.TARGET['board_serials'] if b!=board)
    received=json.loads((root/(peer+'.process_received.json')).read_text());gone=json.loads((root/(peer+'.process_gone.json')).read_text())
    for stage,proof in [('received',received),('gone',gone)]:
        identity={'run_id':run,'nonce':nonce,'board':peer,'peer_role':expected['role'],'stage':stage}
        if any(proof.get(k)!=v for k,v in identity.items()):raise ValueError('peer process proof identity differs')
        if (root/(peer+'.ros.log')).read_text().splitlines().count('CLI_PROCESS_PROOF '+json.dumps(proof))!=1:raise ValueError('peer process proof not bound to log')
    ep=received['endpoint'];gid=ep.get('gid',[]);type_hash=json.loads((root/'type_hashes.json').read_text())['hashes']['std_msgs/msg/String']
    if ep!={'node':expected['node'],'namespace':expected['namespace'],'type':'std_msgs/msg/String','type_hash':type_hash,'gid':gid} or len(gid)!=16 or not any(gid) or any(type(b) is not int or not 0<=b<=255 for b in gid):raise ValueError('CLI-created endpoint metadata differs')
    messages=received['received'];numbers=[]
    if not 3<=len(messages)<=40:raise ValueError('CLI child peer messages missing')
    for message in messages:
        match=re.fullmatch('Hello World: ([1-9][0-9]*)',message)
        if not match or expected['node']+"]: Publishing: '"+message+"'" not in raw:raise ValueError('peer data differs from actual child publication log')
        numbers.append(int(match[1]))
    if numbers!=list(range(numbers[0],numbers[0]+len(numbers))):raise ValueError('CLI child sample order differs')
    if gone.get('node_absent') is not True or gone.get('publisher_absent') is not True:raise ValueError('CLI child graph did not withdraw')
