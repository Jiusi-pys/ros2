"""Issue the service ownership graph case only after both real ROS exits."""
import json
import cli_acceptance as a
from verify_service_qos import validate

CASE='graph:service_client_ownership'


def emit(root,manifest,reports,run,nonce):
    flag=root/'graph_case'
    if not flag.exists():return None
    case_name=flag.read_text().strip();mode=(root/'cli_batch').read_text().strip()
    if case_name==CASE and mode=='service_qos':
        checker=validate;key='service_qos';prefix='SERVICE_QOS_RESULT '
        suffixes=('service_qos.json','service_qos_server.json')
    elif case_name=='graph:endpoint_metadata' and mode=='endpoint_qos':
        from verify_endpoint_qos import validate as checker
        key='endpoint_qos';prefix='ENDPOINT_QOS_RESULT '
        suffixes=('endpoint_qos.json','endpoint_qos.ready','endpoint_qos.go','endpoint_qos.sent','endpoint_qos.observe_go')
    elif case_name=='graph:abrupt_exit' and mode=='graph_abrupt':
        from verify_abrupt_graph import validate as checker
        key='abrupt_graph';prefix='ABRUPT_GRAPH_RESULT '
        suffixes=('abrupt_graph.json','abrupt_before.json','abrupt_after.json','abrupt_after.ready','abrupt_armed.json','peer_armed.json','victim.ready.json','victim.status.json','victim.log','victim_kill.go','hidden_source.json','hidden_source.go','hidden_cli.go','hidden_source.stop','hidden_source.done','hidden_cli.ready','hidden_cli.done')
    elif case_name=='graph:churn' and mode=='graph_churn':
        from verify_churn_graph import validate as checker
        key='churn_graph';prefix='CHURN_GRAPH_RESULT '
        suffixes=('churn_graph.json','hidden_source.json','hidden_source.go','hidden_cli.go','hidden_source.stop','hidden_source.done','hidden_cli.ready','hidden_cli.done')
    elif case_name=='graph:duplicate_node_names' and mode=='graph_duplicate':
        from verify_duplicate_graph import validate as checker
        key='duplicate_graph';prefix='DUPLICATE_GRAPH_RESULT '
        suffixes=('hidden_source.json','duplicate_survivor.json','hidden_source.go','hidden_cli.go','hidden_source.stop','hidden_source.done','hidden_cli.ready','hidden_cli.done')
    else:raise ValueError('unsupported graph case request')
    case=next(c for c in manifest['cases'] if c['id']==case_name)
    executions=[]
    for board in a.TARGET['board_serials']:
        checker(reports[board],root,run,board,nonce)
        staged=a.digest(flag.read_bytes())+'  graph_case'
        if (root/('inputs_'+board+'.sha256')).read_text().splitlines().count(staged)!=1:
            raise ValueError('graph case was not part of frozen board inputs')
        peer=next(b for b in a.TARGET['board_serials'] if b!=board);role='A' if board==a.TARGET['board_serials'][0] else 'B'
        remote='/data/local/tmp/ros2/.mdds-owned-runs/'+run
        argv=['/data/python312-rk3588a/usr/bin/python3.12',remote+'/ros_broker_probe.py','--root',remote,'--run-id',run,'--role',role,'--self-serial',board,'--peer-serial',peer,'--nonce',nonce,'--manifest-sha',a.digest((root/'rclpy_package.json').read_bytes())]
        status=json.loads((root/(board+'.ros.status.json')).read_bytes())
        if any(status.get(k)!=v for k,v in {'run_id':run,'role':'ros','namespace':'/ros_broker_'+run,'returncode':0}.items()):
            raise ValueError('graph ROS process did not complete normally')
        pid,start=status.get('child_pid'),status.get('child_start')
        if type(pid) is not int or pid<=0 or not isinstance(start,str) or not start.isdecimal():raise ValueError('graph ROS process identity missing')
        if (root/(board+'.ros.child.pid')).read_text().strip()!=f'MDDS_OWNED_PROCESS RUN_ID={run} TAG=ros_child PID={pid} START={start}':
            raise ValueError('graph ROS owner record differs')
        path=root/(board+'.ros.log');raw=path.read_text()
        for marker in ('MDDS_GRAPH_ACTUAL_ARGV '+json.dumps(argv),a.terminal_marker(run,case_name,0,argv,board),
                       'GRAPH_PROCESS_EXIT '+json.dumps(status,sort_keys=True)):
            if raw.splitlines().count(marker)!=1:raise ValueError('graph raw argv/exit/terminal evidence missing or ambiguous')
        executions.append({'argv':argv,'board_serial':board,'returncode':0,'child_pid':pid,'child_start':start,
                           'log':{'path':path.name,'sha256':a.digest(path.read_bytes())}})
    receipt={'schema_version':1,'run_id':run,'case_id':case_name,'kind':'functional','status':'PASS',
             'board_serials':a.TARGET['board_serials'],'rmw_implementation':'rmw_mdds','transport':'dsoftbus',
             'executions':executions,'assertions':[{'id':name,'passed':True,'execution':0,
                 'pattern':prefix+json.dumps(reports[a.TARGET['board_serials'][0]][key])} for name in case['assertions']]}
    names=['graph_case','host_report.json']+[board+'.'+suffix for board in reports for suffix in suffixes+('ros.status.json','daemon.log','daemon.inspect.json')]
    receipt['graph_provenance']=[{'path':name,'sha256':a.digest((root/name).read_bytes())} for name in names]
    path=root/(case_name.replace(':','_')+'.receipt.json');path.write_text(json.dumps(receipt,indent=2)+'\n')
    reference={'path':path.name,'sha256':a.digest(path.read_bytes())}
    a.validate_receipt(case,reference,manifest,root)
    case.update(status='PASS',evidence=[reference])
    return case_name
