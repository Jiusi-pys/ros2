"""Validate actual daemon lifecycle, library/socket provenance and CLI receipts."""
import copy
import json
from pathlib import Path
import sys
import re
import cli_acceptance as acceptance
from cli_daemon import node_names, oracle
from cli_daemon_guard import owned
from cli_service_graph import recipe as service_recipe
from cli_node_info import recipe as node_info_recipe

LABELS=['status_before','start','status_running','nodes_cached','nodes_direct']+[r[1] for r in service_recipe('/fixture','B')+node_info_recipe('/fixture','B')]+['stop','status_after','nodes_after_stop']
ABSENT={'domain':175,'daemons':[],'port_bindable':True}


def validate_native_link(raw):
    binds=re.findall(r'^\[mdds/dsoftbus\] OnBind\(',raw,re.MULTILINE)
    shutdowns=re.findall(r'^\[mdds/dsoftbus\] OnShutdown\(',raw,re.MULTILINE)
    if len(binds)!=1 or len(shutdowns)>1:
        raise ValueError('physical link missing or rebound during fixture')


def validate_report(value, root, run, board, nonce):
    validate_native_link((root/(board+'.daemon.log')).read_text())
    remote='/data/local/tmp/ros2/.mdds-owned-runs/'+run
    if any(value.get(k)!=v for k,v in {'run_id':run,'board':board,'nonce':nonce,'passed':True,'before':ABSENT,'after_stop':ABSENT,'after':ABSENT}.items()):
        raise ValueError('daemon batch identity/lifecycle/isolation mismatch')
    if 'emergency_cleanup' in value:raise ValueError('daemon needed emergency cleanup')
    daemon=value['daemon']
    for record in (daemon,value['daemon_after_queries']):
        if not owned(record,remote) or record['owned_udp'] or record['listeners']!=[{'table':'tcp','local':f'0100007F:{11511+175:04X}'}]:
            raise ValueError('daemon process/library/socket identity mismatch')
        expected={remote+'/lib/'+name:acceptance.digest((root/name).read_bytes()) for name in ('libmdds.so','librmw_mdds.so')}
        if record['library_hashes']!=expected:raise ValueError('daemon libraries differ from frozen inputs')
        if record['pid']!=daemon['pid'] or record['start']!=daemon['start']:raise ValueError('daemon replaced during graph queries')
    if value['daemon_exited']!={'pid':daemon['pid'],'start':daemon['start'],'terminated':True}:raise ValueError('daemon exit not tied to owned process')
    if [r['label'] for r in value['results']]!=LABELS:raise ValueError('missing or reordered lifecycle command')
    peer_role='B' if board==acceptance.TARGET['board_serials'][0] else 'A'
    services={label:(case,['ros2']+argv,expected) for case,label,argv,expected in service_recipe('/ros_broker_'+run,peer_role)+node_info_recipe('/ros_broker_'+run,peer_role)}
    for result in value['results']:
        label=result['label'];execution=result['execution'];case=result['case_id']
        if label in services:
            expected_case,argv,expected=services[label]
        elif label.startswith('nodes_'):
            expected=node_names('/ros_broker_'+run);expected_case='cli:node/list'
            argv=['ros2','node','list']+([] if label=='nodes_cached' else ['--no-daemon','--spin-time','3'])
        elif label=='start':expected='The daemon has been started';expected_case='cli:daemon/start';argv=['ros2','daemon','start']
        elif label=='stop':expected='The daemon has been stopped';expected_case='cli:daemon/stop';argv=['ros2','daemon','stop']
        else:expected='The daemon is running' if label=='status_running' else 'The daemon is not running';expected_case='cli:daemon/status';argv=['ros2','daemon','status']
        if case!=expected_case or execution['argv']!=argv or result['expected']!=expected or not result['passed']:
            raise ValueError('wrong CLI lifecycle recipe')
        if execution['returncode']!=0 or execution['board_serial']!=board or execution['child_pid']<=0 or not str(execution['child_start']).isdecimal():raise ValueError('CLI child identity/exit failed')
        log_ref={**execution['log'],'path':board+'.'+execution['log']['path']}
        raw=acceptance.read_artifact(log_ref,root).decode()
        stdout=raw.split('MDDS_CLI_STDOUT_BEGIN\n',1)[1].split('\nMDDS_CLI_STDOUT_END',1)[0]
        if not oracle(case,stdout,expected):raise ValueError('CLI functional output differs')
        if case=='cli:node/list':
            if 'nodes in the graph that share an exact name' not in raw:raise ValueError('duplicate-node warning missing')
        if case=='cli:node/info' and expected['duplicate'] and f'There are 2 nodes in the graph with the exact name "{expected["node"]}".' not in raw:raise ValueError('duplicate-node info warning missing')
        if '--no-daemon' in argv and 'dsoftbus(local=AF_UNIX physical=dsoftbus_broker' not in raw:raise ValueError('direct query did not select DSoftBus')


def main():
    root=Path(sys.argv[1]);run=sys.argv[2];nonce=(root/'nonce').read_text().strip()
    if not json.loads((root/'host_report.json').read_text())['passed']:raise ValueError('physical ROS fixture failed')
    reports={}
    for board in acceptance.TARGET['board_serials']:
        value=json.loads((root/(board+'.cli.results.json')).read_text())
        status=json.loads((root/(board+'.cli.status.json')).read_text())
        record=(root/(board+'.cli.child.pid')).read_text().strip()
        if status['run_id']!=run or status['role']!='cli' or status['returncode']!=0 or record!=f"MDDS_OWNED_PROCESS RUN_ID={run} TAG=cli_child PID={status['child_pid']} START={status['child_start']}":raise ValueError('batch supervisor failed')
        validate_report(value,root,run,board,nonce)
        raw=(root/(board+'.cli.log')).read_text()
        if raw.splitlines().count('CLI_DAEMON_RESULT '+json.dumps(value))!=1:raise ValueError('daemon observation not bound to actual process log')
        reports[board]=value
    manifest=json.loads((root/'cli_acceptance_manifest.json').read_text());manifest['run_id']=run;passed=[]
    for case in manifest['cases']:
        if case['id'] not in ('cli:daemon/start','cli:daemon/status','cli:daemon/stop','cli:node/list','cli:node/info','cli:service/type','cli:service/find','cli:service/info'):continue
        executions=[]
        for board,value in reports.items():
            for result in value['results']:
                if result['case_id']!=case['id']:continue
                execution=copy.deepcopy(result['execution']);execution['log']['path']=board+'.'+execution['log']['path'];executions.append(execution)
        receipt={'schema_version':1,'run_id':run,'case_id':case['id'],'kind':'functional','status':'PASS',
                 'board_serials':acceptance.TARGET['board_serials'],'rmw_implementation':'rmw_mdds','transport':'dsoftbus',
                 'executions':executions,'assertions':[{'id':'functional_result','passed':True,'execution':0,'pattern':'MDDS_CLI_FUNCTIONAL CASE='+case['id']+' RESULT=PASS'}],
                 'supporting_lifecycle':[{'path':board+'.cli.results.json','sha256':acceptance.digest((root/(board+'.cli.results.json')).read_bytes())} for board in reports],
                 'native_link_logs':[{'path':board+'.daemon.log','sha256':acceptance.digest((root/(board+'.daemon.log')).read_bytes())} for board in reports],
                 'supporting_fixture':{'path':'host_report.json','sha256':acceptance.digest((root/'host_report.json').read_bytes())}}
        path=root/(case['id'].replace(':','_').replace('/','_')+'.receipt.json');path.write_text(json.dumps(receipt,indent=2)+'\n')
        ref={'path':path.name,'sha256':acceptance.digest(path.read_bytes())};acceptance.validate_receipt(case,ref,manifest,root)
        case['status']='PASS';case['evidence']=[ref];passed.append(case['id'])
    (root/'cli_partial_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print('CLI_DAEMON_PASS '+json.dumps(passed))


if __name__=='__main__':main()
