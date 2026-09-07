"""Require real SDK loss/recovery plus uninterrupted zero-datagram audit evidence."""
import json
from pathlib import Path
import re
import shutil
import sys
import cli_acceptance as a
from no_udp_contract import validate_policy,validate_counter
from socket_audit_lifetime import validate_lifetime
from verify_socket_audit_probe import check as check_controls
from verify_ros_broker import validate as baseline
from verify_remote_cycle import check as cycle
CASE='transport:no_udp_fallback'
def read(path):return json.loads(path.read_bytes())

def pack(source,root):
    report=check_controls(source,require_runtime=True)
    if not report['passed'] or report!=read(source/'report.json'):raise ValueError('audit positive control report differs')
    if report['library_sha256']!=a.digest((root/'libmdds_test_socket_audit.so').read_bytes()):raise ValueError('audit control tested different library bytes')
    relative=Path('audit_controls')/source.name
    shutil.copytree(source,root/relative,ignore=shutil.ignore_patterns('*.py','*.tar'))
    (root/'audit_control.json').write_text(json.dumps({'directory':relative.as_posix(),'report_sha256':a.digest((source/'report.json').read_bytes()),'library_sha256':report['library_sha256']})+'\n')

def check(root):
    run=root.name;nonce=(root/'nonce').read_text().strip();remote='/data/local/tmp/ros2/.mdds-owned-runs/'+run
    base=baseline(root,run,*a.TARGET['board_serials'])
    if not base['passed'] or base!=read(root/'host_report.json'):raise ValueError('native baseline no longer validates')
    if (root/'policy_mode').read_text().strip()!='selector' or (root/'no_udp.enabled').read_text().strip()!=nonce:raise ValueError('missing explicit no-fallback mode')
    control=read(root/'audit_control.json')
    if not re.fullmatch(r'audit_controls/[A-Za-z0-9_]{1,32}',control['directory']):raise ValueError('invalid control path')
    directory=root/control['directory'];positive=check_controls(directory,require_runtime=True)
    if positive!=read(directory/'report.json') or a.digest((directory/'report.json').read_bytes())!=control['report_sha256']:raise ValueError('positive audit controls changed')
    digest=a.digest((root/'libmdds_test_socket_audit.so').read_bytes())
    if digest!=control['library_sha256'] or digest!=positive['library_sha256']:raise ValueError('untested audit library')
    remote_map={remote+'/lib/libmdds_test_socket_audit.so':digest}
    recovery=cycle(root)
    if 'graph_cycle' not in recovery:raise ValueError('complete graph/RPC recovery not observed')
    results=[];executions=[]
    for board in a.TARGET['board_serials']:
        frozen=(root/('inputs_'+board+'.sha256')).read_text().splitlines()
        for name in ('no_udp.enabled','audit_control.json'):
            data=(root/(board+'.'+name)).read_bytes()
            if data!=(root/name).read_bytes() or frozen.count(a.digest(data)+'  '+name)!=1:raise ValueError('no-UDP inputs were not frozen')
        if frozen.count(digest+'  lib/libmdds_test_socket_audit.so')!=1:raise ValueError('audit was not staged before launch')
        for role in ('ros','daemon'):
            status=read(root/(board+'.'+role+'.status.json'));pid=status['child_pid'];raw=(root/(board+'.'+role+'.log')).read_text();lines=raw.splitlines()
            argv=[json.loads(v.removeprefix('MDDS_GRAPH_ACTUAL_ARGV ')) for v in lines if v.startswith('MDDS_GRAPH_ACTUAL_ARGV ')]
            if len(argv)!=1 or lines.count(a.terminal_marker(run,CASE,0,argv[0],board))!=1 or status['returncode']!=0:raise ValueError('actual no-UDP process/terminal missing')
            final=[json.loads(v.removeprefix('MDDS_SOCKET_AUDIT_FINAL ')) for v in lines if v.startswith('MDDS_SOCKET_AUDIT_FINAL ')]
            if len(final)!=1:raise ValueError('audit final missing')
            validate_counter(final[0],pid);validate_lifetime(lines,pid,argv[0][0],final[0],bootstrap_exec=role=='ros')
            final_line=next(v for v in lines if v.startswith('MDDS_SOCKET_AUDIT_FINAL '))
            if role=='ros':
                observations=[json.loads(v.removeprefix('ROS_BROKER_PROVENANCE ')) for v in lines if v.startswith('ROS_BROKER_PROVENANCE ')]
                if len(observations)!=2:raise ValueError('missing ROS provenance phases')
                for observation in observations:
                    validate_policy(observation['transport_policy']);audit=observation['socket_audit']
                    if audit['mapped']!=remote_map or audit['snapshot']['pid']!=pid:raise ValueError('wrong live ROS audit')
                    value=audit['snapshot']
                    if any(value[k]!=0 for k in ('ipv4_datagram_calls','ipv6_datagram_calls','instrumentation_errors')) or not 0<value['total_calls']<=final[0]['total_calls']:raise ValueError('live ROS audit differs')
                stop='NO_UDP_ROS_STOPPED '+json.dumps({'run_id':run,'nonce':nonce,'board':board,'pid':pid})
            else:
                inspected=read(root/(board+'.daemon.inspect.json'))
                if inspected['socket_audit']!=remote_map or inspected['argv']!=argv[0]:raise ValueError('wrong live native broker audit')
                if lines.count(f'NO_UDP_NATIVE_AUDIT_READY pid={pid}')!=1:raise ValueError('native audit not ready before SDK startup')
                stops=[v for v in lines if v.startswith('MDBC_REMOTE_STOP ')]
                if len(stops)!=1:raise ValueError('native broker did not stop')
                stop=stops[0]
            if lines.count(stop)!=1 or lines.index(stop)>=lines.index(final_line):raise ValueError('audit ended before MDDS shutdown')
            executions.append({'argv':argv[0],'board_serial':board,'returncode':0,'child_pid':pid,'child_start':status['child_start'],'log':{'path':board+'.'+role+'.log','sha256':a.digest(raw.encode())}})
            results.append({'board':board,'role':role,'final':final[0],'final_line':final_line})
    return {'run_id':run,'nonce':nonce,'passed':True,'audit_control':control,'audit':results,'recovery':recovery},executions

def emit(root):
    value,executions=check(root);manifest=read(root/'cli_acceptance_manifest.json');manifest['run_id']=root.name
    case=next(v for v in manifest['cases'] if v['id']==CASE);board=a.TARGET['board_serials'][0]
    ros=(root/(board+'.ros.log')).read_text().splitlines()
    patterns=[('dsoftbus_selected',0,next(v for v in ros if v.startswith('ROS_BROKER_PROVENANCE '))),
              ('failure_observed',0,next(v for v in ros if v.startswith('CYCLE_GRAPH_PAUSED '))),
              ('udp_backend_absent',0,value['audit'][0]['final_line']),
              ('positive_control',0,next(v for v in ros if v.startswith('ROS_BROKER_PHASE ') and json.loads(v.removeprefix('ROS_BROKER_PHASE '))['phase']==2))]
    report=root/'no_udp_report.json';report.write_text(json.dumps(value,indent=2)+'\n')
    names=['no_udp_report.json','host_report.json','audit_control.json','no_udp.enabled','libmdds_test_socket_audit.so','mdds_broker_daemon']
    names += [p.relative_to(root).as_posix() for p in (root/value['audit_control']['directory']).rglob('*') if p.is_file()]
    names += [p.name for p in root.iterdir() if p.is_file() and ('.reconnect.' in p.name or p.name.endswith('.daemon.inspect.json'))]
    receipt={'schema_version':1,'run_id':root.name,'case_id':CASE,'kind':'functional','status':'PASS','board_serials':a.TARGET['board_serials'],
             'rmw_implementation':'rmw_mdds','transport':'dsoftbus','executions':executions,
             'assertions':[{'id':key,'passed':True,'execution':index,'pattern':text} for key,index,text in patterns],
             'artifacts':[{'path':name,'sha256':a.digest((root/name).read_bytes())} for name in sorted(set(names))]}
    path=root/'transport_no_udp_fallback.receipt.json';path.write_text(json.dumps(receipt,indent=2)+'\n');reference={'path':path.name,'sha256':a.digest(path.read_bytes())}
    a.validate_receipt(case,reference,manifest,root);case.update(status='PASS',evidence=[reference]);(root/'cli_partial_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print('NO_UDP_FALLBACK_PASS '+json.dumps({'run_id':root.name,'processes':len(executions),'udp_attempts':0,'graph_recovery':value['recovery']['graph_cycle']}))

if __name__=='__main__':
    if sys.argv[1]=='pack':pack(Path(sys.argv[2]),Path(sys.argv[3]))
    elif sys.argv[1]=='verify':emit(Path(sys.argv[2]))
    else:raise SystemExit('expected pack or verify')
