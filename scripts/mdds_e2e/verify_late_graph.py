"""Late join and multi-node ownership need immutable source/observer evidence."""
import json
import cli_acceptance as a
from late_graph_contract import validate_snapshot
from cli_late_graph import CASES


def validate(value,root,run,board,nonce):
    role='A' if board==a.TARGET['board_serials'][0] else 'B';peer=next(b for b in a.TARGET['board_serials'] if b!=board)
    remote='/data/local/tmp/ros2/.mdds-owned-runs/'+run
    argv=['/data/python312-rk3588a/usr/bin/python3.12','-u','-B',remote+'/board_late_observer.py',remote,run,role,nonce]
    if value.get('argv')!=argv or value.get('returncode')!=0 or value.get('emergency_cleanup') is not False:raise ValueError('late observer command/exit differs')
    if type(value.get('pid')) is not int or value['pid']<=0 or not str(value.get('start')).isdecimal():raise ValueError('late observer process identity invalid')
    if json.loads((root/(board+'.late_observer.status.json')).read_bytes())!={k:v for k,v in value.items() if k!='observation'}:raise ValueError('late observer supervisor status differs')
    observation=value['observation']
    if json.loads((root/(board+'.late_observer.json')).read_bytes())!=observation:raise ValueError('late observer report differs')
    for k,v in {'run_id':run,'nonce':nonce,'role':role,'pid':value['pid'],'start':value['start']}.items():
        if observation.get(k)!=v:raise ValueError('late observer identity differs')
    hashes=json.loads((root/'late_graph_hashes.json').read_bytes())['hashes']
    sources={}
    for source_board in a.TARGET['board_serials']:
        if (root/(source_board+'.late_source.initial.json')).read_bytes()!=(root/(source_board+'.late_source.json')).read_bytes():raise ValueError('source changed after readiness handoff')
        source=json.loads((root/(source_board+'.late_source.json')).read_bytes());source_role='A' if source_board==a.TARGET['board_serials'][0] else 'B'
        status=json.loads((root/(source_board+'.ros.status.json')).read_bytes())
        for k,v in {'run_id':run,'nonce':nonce,'role':source_role,'pid':status['child_pid'],'start':status['child_start'],'context_shared':True}.items():
            if source.get(k)!=v:raise ValueError('source process/context identity differs')
        validate_snapshot(source['snapshot'],run,source_role,hashes)
        if (root/(source_board+'.ros.log')).read_text().splitlines().count('LATE_SOURCE_READY '+json.dumps(source))!=1:raise ValueError('source readiness lacks raw log')
        sources[source_board]=source
    if not sources[board]['ready_ns']<observation['started_ns']<=observation['initialized_ns']<=observation['completed_ns']:raise ValueError('observer was not initialized after readiness')
    if observation['completed_ns']-observation['initialized_ns']>25_000_000_000:raise ValueError('late graph exceeded deadline')
    if value['pid']==sources[board]['pid']:raise ValueError('observer reused source process')
    if observation['own_source_sha256']!=a.digest((root/(board+'.late_source.json')).read_bytes()) or observation['peer_source_sha256']!=a.digest((root/(peer+'.late_source.json')).read_bytes()):raise ValueError('late source proofs changed')
    if (root/(board+'.late_peer_source.json')).read_bytes()!=(root/(peer+'.late_source.json')).read_bytes():raise ValueError('peer readiness mirror differs')
    validate_snapshot(observation['snapshot'],run,sources[peer]['role'],hashes)
    if observation['snapshot']!=sources[peer]['snapshot']:raise ValueError('late graph is not the established graph')
    proof=observation['provenance']
    if proof['pid']!=value['pid'] or proof['libmdds_paths']!=[remote+'/lib/libmdds.so'] or proof['librmw_mdds_paths']!=[remote+'/lib/librmw_mdds.so'] or proof['owned_udp_sockets']!=[]:raise ValueError('late observer middleware/transport differs')
    if proof['rclpy']['manifest_sha256']!=a.digest((root/'rclpy_package.json').read_bytes()):raise ValueError('late observer rclpy differs')
    for marker in ('late_source.go','late_source.stop','late_source.done','late_cli.ready','late_observer.go','late_observer.done'):
        if (root/(board+'.'+marker)).read_text().strip()!=nonce:raise ValueError('late graph barrier differs')
    reference={**value['log'],'path':board+'.'+value['log']['path']};raw=a.read_artifact(reference,root).decode()
    expected='MDDS_GRAPH_ACTUAL_ARGV '+json.dumps(argv)+'\n'+(root/(board+'.late_observer.stdout')).read_text()+'\n'+(root/(board+'.late_observer.stderr')).read_text()+'\n'
    expected+='\n'.join(a.terminal_marker(run,case,0,argv,board) for case in CASES)+'\n'
    if raw!=expected or raw.splitlines().count('LATE_OBSERVER_RESULT '+json.dumps(observation))!=1:raise ValueError('late observer output/terminal differs')


def emit(root,manifest,reports,run,nonce):
    executions=[]
    for board in a.TARGET['board_serials']:
        value=reports[board]['late_graph'];validate(value,root,run,board,nonce)
        executions.append({'argv':value['argv'],'board_serial':board,'returncode':0,'child_pid':value['pid'],'child_start':value['start'],
                           'log':{**value['log'],'path':board+'.'+value['log']['path']}})
    for name in CASES:
        case=next(c for c in manifest['cases'] if c['id']==name)
        receipt={'schema_version':1,'run_id':run,'case_id':name,'kind':'functional','status':'PASS','board_serials':a.TARGET['board_serials'],
                 'rmw_implementation':'rmw_mdds','transport':'dsoftbus','executions':executions,
                 'assertions':[{'id':key,'passed':True,'execution':0,'pattern':'LATE_OBSERVER_RESULT '+json.dumps(reports[a.TARGET['board_serials'][0]]['late_graph']['observation'])} for key in case['assertions']]}
        files=['late_graph_hashes.json','host_report.json','libmdds.so','librmw_mdds.so']+[b+'.'+suffix for b in reports for suffix in ('late_source.initial.json','late_source.json','late_peer_source.json','late_observer.json','late_observer.status.json','late_source.go','late_observer.go','late_source.stop','late_source.done','daemon.log','daemon.inspect.json')]
        receipt['graph_provenance']=[{'path':p,'sha256':a.digest((root/p).read_bytes())} for p in files]
        path=root/(name.replace(':','_')+'.receipt.json');path.write_text(json.dumps(receipt,indent=2)+'\n');ref={'path':path.name,'sha256':a.digest(path.read_bytes())}
        a.validate_receipt(case,ref,manifest,root);case.update(status='PASS',evidence=[ref])
    return list(CASES)
