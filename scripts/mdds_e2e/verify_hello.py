"""Keep ROS peer delivery distinct from hello's intentional UDP diagnostics."""
from collections import Counter
import json
import re
import cli_acceptance as a
from cli_hello import COMMANDS,summary


def validate(value,root,run,board,nonce):
    peer=next(b for b in a.TARGET['board_serials'] if b!=board)
    peer_results=json.loads((root/(peer+'.cli.results.json')).read_text())
    manifest=json.loads((root/'doctor_manifest.json').read_text());application=manifest['application']
    if a.digest((root/'doctor_application.zip').read_bytes())!=application['sha256']:raise ValueError('hello application archive differs')
    runtime=json.loads((root/(board+'.doctor_runtime.json')).read_text())
    if runtime['manifest_sha256']!=a.digest((root/'doctor_manifest.json').read_bytes()) or runtime['application_sha256']!=application['sha256']:raise ValueError('hello application provenance differs')
    for command in COMMANDS:
        label='hello_'+command;record=next(r for r in value['results'] if r['label']==label);e=record['execution'];expected=record['expected'];detail=e['hello_process'];native=detail['native']
        other=next(r for r in peer_results['results'] if r['label']==label)['execution']
        for source in (board,peer):
            for stage in ('ready','start','observed','stop'):
                if (root/(source+'.'+label+'.'+stage)).read_text().strip()!=nonce:raise ValueError('hello barrier differs')
        remote='/data/local/tmp/ros2/.mdds-owned-runs/'+run;hashes={remote+'/lib/'+name:a.digest((root/name).read_bytes()) for name in ('libmdds.so','librmw_mdds.so')}
        if native['pid']!=e['child_pid'] or native['start']!=e['child_start'] or native['hashes']!=hashes or detail['emergency_cleanup']:raise ValueError('hello native process differs')
        udp=native['diagnostic_udp']
        if len(udp)!=2 or any(row['table']!='udp' or row['remote']!='00000000:0000' for row in udp) or sum(row['local']=='00000000:'+format(expected['port'],'04X') for row in udp)!=1:raise ValueError('hello diagnostic sockets differ')
        raw=a.read_artifact({**e['log'],'path':board+'.'+e['log']['path']},root).decode();stdout=raw.split('MDDS_CLI_STDOUT_BEGIN\n',1)[1].split('\nMDDS_CLI_STDOUT_END',1)[0]
        if 'Traceback' in raw or 'Exception in thread' in raw or 'dsoftbus(local=AF_UNIX physical=dsoftbus_broker' not in raw:raise ValueError('hello runtime error or wrong transport')
        observed=detail['observed_stdout']
        if not isinstance(observed,str) or not stdout.startswith(observed) or summary(observed,expected) is None:raise ValueError('hello summary lacks both peer lanes')
        if raw.splitlines().count('MDDS_HELLO_PROCESS '+json.dumps(detail))!=1:raise ValueError('hello process log missing')
        received=json.loads((root/(board+'.'+label+'_received.json')).read_text());gone=json.loads((root/(board+'.'+label+'_gone.json')).read_text())
        for stage,proof in (('received',received),('gone',gone)):
            identity={'run_id':run,'nonce':nonce,'board':board,'peer_role':expected['peer_id'][0],'command':command,'stage':stage}
            if any(proof.get(k)!=v for k,v in identity.items()) or (root/(board+'.ros.log')).read_text().splitlines().count('CLI_HELLO_PROOF '+json.dumps(proof))!=1:raise ValueError('hello ROS observer identity differs')
        if not 3<=len(received['received'])<=300 or any(text!="hello, it's me "+expected['peer_id'] for text in received['received']):raise ValueError('hello ROS payload differs')
        expected_nodes=[]
        for execution in (e,other):
            host=execution['hello_process']['native']['hostname'];expected_nodes.append('ros2doctor_'+re.sub('[^0-9a-zA-Z_]','_',host)+'_'+str(execution['child_pid'])+'_node')
        publishers=received['publishers'];type_hash=json.loads((root/'type_hashes.json').read_text())['hashes']['std_msgs/msg/String']
        if len(publishers)!=2 or Counter(p['node'] for p in publishers)!=Counter(expected_nodes) or any(p['namespace']!='/' or p['type']!='std_msgs/msg/String' or p['type_hash']!=type_hash or len(p['gid'])!=16 or not any(p['gid']) for p in publishers):raise ValueError('hello publisher graph metadata differs')
        if gone.get('publishers_absent') is not True or gone.get('nodes_absent') is not True:raise ValueError('hello graph did not withdraw')
