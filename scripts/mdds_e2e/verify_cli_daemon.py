"""Validate actual daemon lifecycle, library/socket provenance and CLI receipts."""
import copy
import json
from pathlib import Path
import sys
import re
import cli_acceptance as acceptance
from cli_daemon import node_names, oracle, batch_recipe
from cli_action import goal_id, FEEDBACK, RESULT
from cli_service_events import events_match
from cli_parameters import values as parameter_values,typed_equal
from cli_parameter_changes import loaded_values,expected_events
from cli_lifecycle import expected_callbacks,expected_events as lifecycle_events
from cli_components import containers
import yaml
from cli_daemon_guard import owned
from cli_service_graph import recipe as service_recipe
from cli_node_info import recipe as node_info_recipe

ABSENT={'domain':175,'daemons':[],'port_bindable':True}


def validate_native_link(raw):
    binds=re.findall(r'^\[mdds/dsoftbus\] OnBind\(',raw,re.MULTILINE)
    shutdowns=re.findall(r'^\[mdds/dsoftbus\] OnShutdown\(',raw,re.MULTILINE)
    if len(binds)!=1 or len(shutdowns)>1:
        raise ValueError('physical link missing or rebound during fixture')


def validate_report(value, root, run, board, nonce):
    if (root/'ros2cli_overlay.json').exists():
        overlay=json.loads((root/'ros2cli_overlay.json').read_text())
        if acceptance.digest((root/'ros2cli_overlay.zip').read_bytes())!=overlay['archive_sha256']:raise ValueError('CLI source overlay archive differs')
        ready=json.loads((root/(board+'.ros2cli_overlay_ready.json')).read_text())
        if ready!={'run_id':run,'manifest_sha256':acceptance.digest((root/'ros2cli_overlay.json').read_bytes()),'files':len(overlay['files'])}:raise ValueError('CLI source overlay readiness differs')
    validate_native_link((root/(board+'.daemon.log')).read_text())
    remote='/data/local/tmp/ros2/.mdds-owned-runs/'+run
    if any(value.get(k)!=v for k,v in {'run_id':run,'board':board,'nonce':nonce,'passed':True,'before':ABSENT,'after_stop':ABSENT,'after':ABSENT}.items()):
        raise ValueError('daemon batch identity/lifecycle/isolation mismatch')
    if 'emergency_cleanup' in value:raise ValueError('daemon needed emergency cleanup')
    if (root/'cli_batch').read_text().strip()=='hello':
        from verify_hello import validate
        validate(value,root,run,board,nonce)
    if (root/'cli_batch').read_text().strip()=='diagnostics':
        from verify_doctor import validate
        validate(value,root,run,board)
    if (root/'cli_batch').read_text().strip() in ('process_run','process_launch','process_test') and value.get('process_start_nonce')!=nonce:raise ValueError('process start barrier missing')
    if (root/'cli_batch').read_text().strip()=='statistics':
        from verify_topic_statistics import validate
        validate(value,root,run,board,nonce)
    if (root/'cli_batch').read_text().strip()=='standalone' and value.get('standalone_start_nonce')!=nonce:raise ValueError('standalone started without matching barrier')
    if (root/'cli_batch').read_text().strip()=='components':
        from verify_components import validate
        validate(value,root,run,board,nonce)
    if (root/'cli_batch').read_text().strip() in ('bags','bag_transform','bag_burst'):
        from verify_bags import validate
        validate(value,root,run,board,nonce)
        if (root/'cli_batch').read_text().strip()=='bag_transform':
            from verify_bag_transform import validate
            validate(value,root,run,board)
        if (root/'cli_batch').read_text().strip()=='bag_burst':
            from verify_bag_burst import validate
            validate(value,root,run,board,nonce)
    daemon=value['daemon']
    for record in (daemon,value['daemon_after_queries']):
        if not owned(record,remote) or record['owned_udp'] or record['listeners']!=[{'table':'tcp','local':f'0100007F:{11511+175:04X}'}]:
            raise ValueError('daemon process/library/socket identity mismatch')
        expected={remote+'/lib/'+name:acceptance.digest((root/name).read_bytes()) for name in ('libmdds.so','librmw_mdds.so')}
        if record['library_hashes']!=expected:raise ValueError('daemon libraries differ from frozen inputs')
        if record['pid']!=daemon['pid'] or record['start']!=daemon['start']:raise ValueError('daemon replaced during graph queries')
    if value['daemon_exited']!={'pid':daemon['pid'],'start':daemon['start'],'terminated':True}:raise ValueError('daemon exit not tied to owned process')
    peer_role='B' if board==acceptance.TARGET['board_serials'][0] else 'A'
    recipes=batch_recipe((root/'cli_batch').read_text().strip(),'/ros_broker_'+run,peer_role,nonce)
    labels=['status_before','start','status_running','nodes_cached','nodes_direct']+[r[1] for r in recipes]+['stop','status_after','nodes_after_stop']
    if [r['label'] for r in value['results']]!=labels:raise ValueError('missing or reordered lifecycle command')
    services={label:(case,['ros2']+argv,expected) for case,label,argv,expected in recipes}
    for result in value['results']:
        label=result['label'];execution=result['execution'];case=result['case_id']
        if label in services:
            expected_case,argv,expected=services[label]
        elif label.startswith('nodes_'):
            expected=node_names('/ros_broker_'+run);expected_case='cli:node/list'
            if (root/'cli_batch').read_text().strip()=='components':expected+=containers('/ros_broker_'+run)
            argv=['ros2','node','list']+([] if label=='nodes_cached' else ['--no-daemon','--spin-time','3'])
        elif label=='start':expected='The daemon has been started';expected_case='cli:daemon/start';argv=['ros2','daemon','start']
        elif label=='stop':expected='The daemon has been stopped';expected_case='cli:daemon/stop';argv=['ros2','daemon','stop']
        else:expected='The daemon is running' if label=='status_running' else 'The daemon is not running';expected_case='cli:daemon/status';argv=['ros2','daemon','status']
        if case!=expected_case or execution['argv']!=argv or result['expected']!=expected or not result['passed']:
            raise ValueError('wrong CLI lifecycle recipe')
        absent=case=='cli:param/delete' and expected.get('kind')=='absent'
        controlled=case in ('cli:service/echo','cli:topic/hz','cli:topic/bw','cli:topic/delay','cli:doctor/hello','cli:wtf/hello')
        if execution['returncode'] not in ((1,) if absent else ((0,2) if controlled else (0,))) or execution['board_serial']!=board or execution['child_pid']<=0 or not str(execution['child_start']).isdecimal():raise ValueError('CLI child identity/exit failed')
        log_ref={**execution['log'],'path':board+'.'+execution['log']['path']}
        raw=acceptance.read_artifact(log_ref,root).decode()
        if absent:
            index=value['results'].index(result)
            acceptance.validate_parameter_absence(execution,[r['execution'] for r in value['results'][:index]],raw)
        begin='MDDS_CLI_STDOUT_BEGIN\n';end='\nMDDS_CLI_STDOUT_END'
        if raw.count(begin)!=1 or raw.count(end)!=1:raise ValueError('CLI captured stdout boundaries differ')
        stdout=raw.split(begin,1)[1].split(end,1)[0]
        if case=='cli:service/echo':
            record=json.loads((root/(board+'.introspection.result.json')).read_text())
            wanted={**expected,'run_id':run,'nonce':nonce,'board':board,'client_gid':record.get('client_gid'),'sequence_number':record.get('sequence_number')}
            if record!=wanted or not events_match(stdout,record):raise ValueError('service event payload/identity differs')
            if (root/(board+'.ros.log')).read_text().splitlines().count('CLI_INTROSPECTION_CLIENT '+json.dumps(record))!=1:raise ValueError('client introspection record missing')
            peer_board=next(other for other in acceptance.TARGET['board_serials'] if other!=board)
            server=json.loads((root/(peer_board+'.introspection.server.json')).read_text())
            if server!={**expected,'run_id':run,'nonce':nonce,'board':peer_board,'count':1}:raise ValueError('peer service callback differs')
            if (root/(peer_board+'.ros.log')).read_text().splitlines().count('CLI_INTROSPECTION_SERVER '+json.dumps(server))!=1:raise ValueError('peer service callback missing')
        elif case=='cli:bag/record':pass  # Native storage and exact byte-level samples checked above.
        elif case=='cli:security/generate_policy':
            from cli_policy import expected_policy,validate_policy
            reference=execution['policy_file']
            raw_policy=acceptance.read_artifact({**reference,'path':board+'.'+reference['path']},root).decode()
            validate_policy(raw_policy,expected_policy(run))
        elif case in ('cli:run','cli:launch','cli:test'):
            from verify_process import validate
            validate(execution,expected,raw,root,run,board,nonce)
        elif case=='cli:component/standalone':
            from verify_standalone import validate
            validate(execution,expected,raw,root,run,board,nonce)
        elif not oracle(case,stdout,expected):raise ValueError('CLI functional output differs')
        if case=='cli:node/list':
            if 'nodes in the graph that share an exact name' not in raw:raise ValueError('duplicate-node warning missing')
        if case=='cli:node/info' and expected['duplicate'] and f'There are 2 nodes in the graph with the exact name "{expected["node"]}".' not in raw:raise ValueError('duplicate-node info warning missing')
        if ('--no-daemon' in argv or case.startswith('cli:param/') or case in ('cli:action/send_goal','cli:service/echo','cli:bag/record','cli:bag/play','cli:lifecycle/get','cli:lifecycle/list','cli:lifecycle/set','cli:component/load','cli:component/list','cli:component/unload')) and 'dsoftbus(local=AF_UNIX physical=dsoftbus_broker' not in raw:raise ValueError('direct query did not select DSoftBus')
        if case.startswith('cli:lifecycle/'):
            peer_board=next(other for other in acceptance.TARGET['board_serials'] if other!=board)
            node='/ros_broker_'+run+'/alpha_'+peer_role
            identity={'run_id':run,'nonce':nonce,'board':peer_board,'node':node}
            final=json.loads((root/(peer_board+'.lifecycle_final.json')).read_text())
            callbacks=json.loads((root/(peer_board+'.lifecycle_callbacks.json')).read_text())
            events=json.loads((root/(board+'.lifecycle_events.json')).read_text())
            if final!={**identity,'state':{'id':4,'label':'finalized'}} or callbacks!={**identity,'callbacks':expected_callbacks()} or events!={**identity,'board':board,'events':lifecycle_events()}:raise ValueError('lifecycle state/callback/events differ')
            peer_log=(root/(peer_board+'.ros.log')).read_text().splitlines()
            event_log=(root/(board+'.ros.log')).read_text().splitlines()
            if peer_log.count('CLI_LIFECYCLE_FINAL '+json.dumps(final))!=1 or any(peer_log.count('CLI_LIFECYCLE_CALLBACK '+json.dumps(item))!=1 for item in callbacks['callbacks']) or any(event_log.count('CLI_LIFECYCLE_EVENT '+json.dumps(item))!=1 for item in events['events']):raise ValueError('lifecycle raw evidence missing')
        if case.startswith('cli:param/'):
            peer_board=next(other for other in acceptance.TARGET['board_serials'] if other!=board)
            state=json.loads((root/(peer_board+'.parameter_state.json')).read_text())
            wanted={'run_id':run,'nonce':nonce,'board':peer_board,'node':'/ros_broker_'+run+'/alpha_'+peer_role,'values':parameter_values(nonce,peer_role)}
            if not typed_equal(state,wanted):raise ValueError('peer parameter state differs from fixture contract')
            if case=='cli:param/dump' and (root/(board+'.parameters_dump.yaml')).read_text()!=stdout:raise ValueError('dump file differs from actual CLI output')
            if case in ('cli:param/set','cli:param/load','cli:param/delete'):
                final_values={**parameter_values(nonce,peer_role),**loaded_values(nonce,peer_role)};final_values.pop('ephemeral')
                final=json.loads((root/(peer_board+'.parameter_final.json')).read_text())
                if not typed_equal(final,{**wanted,'values':final_values}):raise ValueError('peer final parameter state differs')
                if (root/(peer_board+'.ros.log')).read_text().splitlines().count('CLI_PARAMETER_FINAL '+json.dumps(final))!=1:raise ValueError('final parameter state log missing')
                events=json.loads((root/(board+'.parameter_events.json')).read_text())
                required_events=expected_events('/ros_broker_'+run,peer_role,nonce)
                if not typed_equal(events,{'run_id':run,'nonce':nonce,'board':board,'events':required_events}):raise ValueError('parameter events differ from set/restore/load/delete sequence')
                source_log=(root/(board+'.ros.log')).read_text().splitlines()
                if any(source_log.count('CLI_PARAMETER_EVENT '+json.dumps(event))!=1 for event in events['events']):raise ValueError('raw parameter event missing or duplicated')
                expected_yaml={wanted['node']:{'ros__parameters':loaded_values(nonce,peer_role)}}
                if not typed_equal(yaml.safe_load((root/(board+'.parameter_load.yaml')).read_text()),expected_yaml):raise ValueError('parameter load file differs')
        if case=='cli:action/send_goal':
            peer_board=next(other for other in acceptance.TARGET['board_serials'] if other!=board)
            received=json.loads((root/(peer_board+'.action_goal.json')).read_text())
            wanted={'run_id':run,'nonce':nonce,'board':peer_board,'action':'/ros_broker_'+run+'/'+peer_role+'/cli_action',
                    'goal_id':goal_id(stdout),'order':5,'feedback':FEEDBACK,'result':RESULT,'status':'SUCCEEDED','count':1}
            if received!=wanted:raise ValueError('peer goal execution differs from CLI result')
            if (root/(peer_board+'.ros.log')).read_text().splitlines().count('CLI_ACTION_GOAL '+json.dumps(received))!=1:raise ValueError('peer goal callback log missing')


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
    executed={r['case_id'] for value in reports.values() for r in value['results']}
    for case in manifest['cases']:
        if case['id'] not in executed:continue
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
        if case['id']=='cli:action/send_goal':
            receipt['peer_goal_executions']=[{'path':board+'.action_goal.json','sha256':acceptance.digest((root/(board+'.action_goal.json')).read_bytes())} for board in reports]
            raw=acceptance.read_artifact(executions[0]['log'],root).decode()
            stdout=raw.split('MDDS_CLI_STDOUT_BEGIN\n',1)[1].split('\nMDDS_CLI_STDOUT_END',1)[0]
            sequence=''.join('- '+str(number)+'\n' for number in RESULT)
            patterns={'goal_accepted':'Goal accepted with ID: '+goal_id(stdout),
                      'feedback_received':'Feedback:\n    partial_sequence:\n'+sequence,
                      'result_exact':'Result:\n    sequence:\n'+sequence,
                      'status_succeeded':'Goal finished with status: SUCCEEDED'}
            receipt['assertions']=[{'id':key,'passed':True,'execution':0,'pattern':text} for key,text in patterns.items()]
        if case['id']=='cli:service/echo':
            receipt['service_transactions']=[{'path':board+'.introspection.'+kind+'.json','sha256':acceptance.digest((root/(board+'.introspection.'+kind+'.json')).read_bytes())} for board in reports for kind in ('result','server')]
        if case['id'].startswith('cli:param/'):
            receipt['peer_parameter_state']=[{'path':board+'.parameter_state.json','sha256':acceptance.digest((root/(board+'.parameter_state.json')).read_bytes())} for board in reports]
            if case['id']=='cli:param/dump':receipt['dump_files']=[{'path':board+'.parameters_dump.yaml','sha256':acceptance.digest((root/(board+'.parameters_dump.yaml')).read_bytes())} for board in reports]
            if case['id'] in ('cli:param/set','cli:param/load','cli:param/delete'):
                receipt['mutation_evidence']=[{'path':board+'.'+suffix,'sha256':acceptance.digest((root/(board+'.'+suffix)).read_bytes())} for board in reports for suffix in ('parameter_final.json','parameter_events.json','parameter_load.yaml')]
        if case['id'].startswith('cli:lifecycle/'):
            receipt['lifecycle_evidence']=[{'path':board+'.'+suffix,'sha256':acceptance.digest((root/(board+'.'+suffix)).read_bytes())} for board in reports for suffix in ('lifecycle_final.json','lifecycle_callbacks.json','lifecycle_events.json')]
        if case['id'].startswith('cli:component/'):
            suffixes=('standalone_received.json','standalone_gone.json','standalone.ready','standalone.start','standalone.stop') if case['id']=='cli:component/standalone' else ('components_loaded.json','components_retired.json','components_empty.json','container.log','container.status.json')
            receipt['component_evidence']=[{'path':board+'.'+suffix,'sha256':acceptance.digest((root/(board+'.'+suffix)).read_bytes())} for board in reports for suffix in suffixes]
        if case['id'] in ('cli:run','cli:launch','cli:test'):
            receipt['process_evidence']=[{'path':board+'.'+suffix,'sha256':acceptance.digest((root/(board+'.'+suffix)).read_bytes())} for board in reports for suffix in ('process_received.json','process_gone.json','process.ready','process.start','process.stop')]
            if case['id']=='cli:launch':receipt['launch_definition']={'path':'process_talker.launch.py','sha256':acceptance.digest((root/'process_talker.launch.py').read_bytes())}
            if case['id']=='cli:test':
                names=[board+'.'+suffix for board in reports for suffix in ('process_test.junit.xml','process_test_config.json','process_test_assertions.json')]+['peer_talker_test.py','mdds_cli_fixture.package.xml','mdds_cli_fixture.marker']
                receipt['launch_test_evidence']=[{'path':name,'sha256':acceptance.digest((root/name).read_bytes())} for name in names]
        if case['id'].startswith('cli:bag/'):
            receipt['bag_artifacts']=[{'path':board+'.'+item['path'].replace('/','_'),'sha256':item['sha256']} for board,value in reports.items() for item in value['bag_files']]
            if case['id']=='cli:bag/burst':
                names=[board+'.'+name for board in reports for storage in ('sqlite3','mcap') for name in ('bag_'+storage+'_burst.json','bag_'+storage+'.burst_stop','bag_burst_'+storage+'.yaml')]
                receipt['burst_artifacts']=[{'path':name,'sha256':acceptance.digest((root/name).read_bytes())} for name in names]
            if case['id'] in ('cli:bag/convert','cli:bag/reindex'):
                artifacts=[]
                for board,value in reports.items():
                    for result in value['results']:
                        if result['case_id']!=case['id']:continue
                        suffixes=['.inspection.json']+(['.yaml'] if case['id']=='cli:bag/convert' else [])
                        for suffix in suffixes:
                            path=board+'.'+result['label']+suffix
                            artifacts.append({'path':path,'sha256':acceptance.digest((root/path).read_bytes())})
                receipt['transformation_artifacts']=artifacts
        if case['id'] in ('cli:topic/hz','cli:topic/bw','cli:topic/delay'):
            verb=case['id'].split('/')[-1]
            names=[board+'.stats_'+verb+suffix for board in reports for suffix in ('_sent.json','_received.json','.go')]
            receipt['statistics_artifacts']=[{'path':name,'sha256':acceptance.digest((root/name).read_bytes())} for name in names]
        if case['id'] in ('cli:doctor','cli:wtf'):
            names=['doctor_manifest.json','doctor_environment.env','doctor_reference/index-v4.yaml','doctor_reference/jazzy/distribution.yaml']+[board+'.doctor_runtime.json' for board in reports]
            receipt['diagnostic_inputs']=[{'path':name,'sha256':acceptance.digest((root/name).read_bytes())} for name in names]
        if case['id'] in ('cli:doctor/hello','cli:wtf/hello'):
            command=case['id'].split(':')[1].split('/')[0]
            names=['doctor_manifest.json','doctor_application.zip']+[board+'.hello_'+command+suffix for board in reports for suffix in ('_received.json','_gone.json','.ready','.start','.observed','.stop')]
            receipt['hello_evidence']=[{'path':name,'sha256':acceptance.digest((root/name).read_bytes())} for name in names]
        if case['id']=='cli:security/generate_policy':
            names=[board+'.policy_'+mode+'.xml' for board in reports for mode in ('cached','direct')]
            receipt['policy_artifacts']=[{'path':name,'sha256':acceptance.digest((root/name).read_bytes())} for name in names]
        if (root/'ros2cli_overlay.json').exists():
            names=['ros2cli_overlay.json','ros2cli_overlay.zip']+[board+'.ros2cli_overlay_ready.json' for board in reports]
            receipt['cli_source_overlay']=[{'path':name,'sha256':acceptance.digest((root/name).read_bytes())} for name in names]
        path=root/(case['id'].replace(':','_').replace('/','_')+'.receipt.json');path.write_text(json.dumps(receipt,indent=2)+'\n')
        ref={'path':path.name,'sha256':acceptance.digest(path.read_bytes())};acceptance.validate_receipt(case,ref,manifest,root)
        case['status']='PASS';case['evidence']=[ref];passed.append(case['id'])
    (root/'cli_partial_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print('CLI_DAEMON_PASS '+json.dumps(passed))


if __name__=='__main__':main()
