"""Real ROS CLI daemon lifecycle and cached/direct graph comparison."""
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time
import cli_acceptance as acceptance
from board_graph_ownership import process_start
from cli_daemon_guard import assert_absent, domain_daemons, observe, owned, retire
from cli_service_graph import oracle as service_oracle, recipe as service_recipe
from cli_node_info import oracle as node_info_oracle, recipe as node_info_recipe
from cli_action import oracle as action_oracle, recipe as action_recipe
from cli_service_echo import recipe as echo_recipe, execute as execute_echo
from cli_parameters import recipe as parameter_recipe,oracle as parameter_oracle
from cli_parameter_changes import recipe as parameter_change_recipe,oracle as parameter_change_oracle,loaded_values
from cli_lifecycle import recipe as lifecycle_recipe,oracle as lifecycle_oracle
from cli_components import recipe as component_recipe,oracle as component_oracle,containers
from cli_standalone import recipe as standalone_recipe,execute as execute_standalone
from cli_bag import recipe as bag_recipe,oracle as bag_oracle
from bag_record import execute as execute_record,freeze_files as freeze_bag_files


def batch_recipe(mode,ns,peer,nonce=''):
    if mode=='endpoint_qos':return []
    if mode=='graph_late':return []
    if mode in ('graph_waiters','graph_remote'):return []
    if mode=='service_qos':return []
    if mode=='trace_probe':
        from trace_contract import recipe
        return recipe(ns.removeprefix('/ros_broker_'),nonce)
    if mode=='multicast':
        from cli_multicast import recipe
        return recipe(ns,peer,nonce)
    if mode=='policy':
        from cli_policy import recipe
        return recipe(ns,peer,nonce)
    if mode=='hello':
        from cli_hello import recipe
        return recipe(ns,peer,nonce)
    if mode=='diagnostics':
        from cli_doctor import recipe
        return recipe(ns,peer,nonce)
    if mode=='process_test':
        from cli_process import recipe
        return recipe(ns,peer,mode='test')
    if mode=='process_launch':
        from cli_process import recipe
        return recipe(ns,peer,mode='launch')
    if mode=='process_run':
        from cli_process import recipe
        return recipe(ns,peer)
    if mode=='statistics':
        from topic_statistics import recipe
        return recipe(ns,peer,nonce)
    if mode=='bag_burst':
        from bag_burst import recipe
        return recipe(ns,peer,nonce)
    if mode=='bag_transform':
        from bag_transform import recipe
        return recipe(ns,peer,nonce)
    if mode=='bags':return bag_recipe(ns,peer,nonce)
    if mode=='standalone':return standalone_recipe(ns,peer)
    if mode=='components':return component_recipe(ns,peer)
    if mode=='lifecycle':return lifecycle_recipe(ns,peer)
    if mode=='parameter_write':return parameter_change_recipe(ns,peer,nonce)
    if mode=='parameter_read':return parameter_recipe(ns,peer,nonce)
    if mode=='introspection':return echo_recipe(ns,peer,nonce)
    if mode=='action':return action_recipe(ns,peer)
    if mode=='daemon':return service_recipe(ns,peer)+node_info_recipe(ns,peer)
    raise ValueError('unknown CLI daemon batch')


def node_names(namespace):
    return [namespace+'/'+name+'_'+role for role in ('A','B') for name in ('alpha','beta','duplicate','duplicate')]


def oracle(case, stdout, expected):
    if case=='cli:multicast/send':return stdout.strip()=='Sending one UDP multicast datagram...'
    if case=='cli:multicast/receive':
        from cli_multicast import received_packet
        return received_packet(stdout,expected['peer_ip']) is not None
    if case=='cli:security/generate_policy':return True  # Exact policy artifact verification is mandatory.
    if case in ('cli:doctor/hello','cli:wtf/hello'):
        from cli_hello import summary
        return summary(stdout,expected) is not None
    if case in ('cli:doctor','cli:wtf'):
        from cli_doctor import oracle as doctor_oracle
        return doctor_oracle(stdout,expected)
    if case in ('cli:topic/hz','cli:topic/bw','cli:topic/delay'):return True  # Independently checked against peer fixture records.
    if case=='cli:bag/burst':return True  # Exact peer proof and controlled player exit are mandatory.
    if case in ('cli:bag/convert','cli:bag/reindex'):return True  # Mandatory native and independent file checks follow.
    if case.startswith('cli:bag/'):return bag_oracle(case,stdout,expected)
    if case.startswith('cli:component/'):return component_oracle(stdout,expected)
    if case.startswith('cli:lifecycle/'):return lifecycle_oracle(stdout,expected)
    if case in ('cli:param/set','cli:param/load','cli:param/delete'):return parameter_change_oracle(stdout,expected)
    if case.startswith('cli:param/'):return parameter_oracle(case,stdout,expected)
    if case.startswith('cli:action/'):return action_oracle(case,stdout,expected)
    if case=='cli:node/info':return node_info_oracle(stdout,expected)
    if case.startswith('cli:service/'):return service_oracle(case,stdout,expected)
    lines = [line.strip() for line in stdout.splitlines() if line.strip()]
    if case == 'cli:node/list': return Counter(lines) == Counter(expected)
    if case in ('cli:daemon/start','cli:daemon/status','cli:daemon/stop'): return lines == [expected]
    return False


def execute(argv, directory, run, board, case, label, expected):
    actual = [sys.executable,'-B','-c','from ros2cli.cli import main; raise SystemExit(main())']+argv[1:]
    with subprocess.Popen(actual, stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True) as child:
        start = process_start(child.pid)
        def stop(signum, frame):
            if child.poll() is None: os.killpg(child.pid, signal.SIGKILL)
            child.wait()
            raise SystemExit(128+signum)
        previous = {s:signal.signal(s,stop) for s in (signal.SIGINT,signal.SIGTERM)}
        try:
            try: stdout, stderr = child.communicate(timeout=20)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid,signal.SIGKILL)
                stdout, stderr = child.communicate()
        finally:
            for s,handler in previous.items(): signal.signal(s,handler)
        stdout, stderr = stdout.decode(), stderr.decode()
        absent=case=='cli:param/delete' and expected.get('kind')=='absent'
        passed = child.returncode == (1 if absent else 0) and oracle(case,stdout,expected)
        policy_file=None;policy_error=None
        if case=='cli:security/generate_policy' and passed:
            from cli_policy import expected_policy,validate_policy
            path=directory.parent/('policy_'+expected['mode']+'.xml')
            try:
                validate_policy(path.read_text(),expected_policy(run))
                policy_file={'path':path.name,'sha256':acceptance.digest(path.read_bytes())}
            except (OSError,ValueError) as error:passed=False;policy_error=str(error)
        if absent:
            errors=[line.strip() for line in stderr.splitlines() if line.strip() and not line.startswith('[INFO] [rmw_mdds]: mdds transports active: dsoftbus(')]
            passed=passed and errors==['Parameter not set']
        if case == 'cli:node/list':
            passed = passed and 'nodes in the graph that share an exact name' in stderr
        if case == 'cli:node/info' and expected['duplicate']:
            passed = passed and f'There are 2 nodes in the graph with the exact name "{expected["node"]}".' in stderr
        if '--no-daemon' in argv or case in ('cli:action/send_goal','cli:bag/play') or case.startswith('cli:param/') or case in ('cli:lifecycle/get','cli:lifecycle/list','cli:lifecycle/set','cli:component/load','cli:component/list','cli:component/unload'):
            passed = passed and 'dsoftbus(local=AF_UNIX physical=dsoftbus_broker' in stderr
        marker = 'MDDS_CLI_FUNCTIONAL CASE='+case+' RESULT=PASS'
        log = directory / (label+'.log')
        text = ('MDDS_CLI_ACTUAL_ARGV '+json.dumps(actual)+'\nMDDS_CLI_STDOUT_BEGIN\n'+stdout+
                '\nMDDS_CLI_STDOUT_END\nMDDS_CLI_STDERR_BEGIN\n'+stderr+'\nMDDS_CLI_STDERR_END\n'+
                acceptance.terminal_marker(run,case,child.returncode,argv,board)+'\n')
        if passed: text += marker+'\n'
        log.write_text(text,encoding='utf-8')
        if case=='cli:param/dump' and passed:
            (directory/'parameters_dump.yaml').write_text(stdout,encoding='utf-8')
        value={'case_id':case,'label':label,'expected':expected,'passed':passed,'execution':{
            'argv':argv,'actual_argv':actual,'child_pid':child.pid,'child_start':start,'board_serial':board,
            'returncode':child.returncode,'log':{'path':log.name,'sha256':acceptance.digest(log.read_bytes())}}}
        if absent:value['execution']['expected_failure']='parameter_not_set'
        if policy_file:value['execution']['policy_file']=policy_file
        if policy_error:value['policy_error']=policy_error
        return value


def inspect_daemon(root):
    deadline=time.monotonic()+5
    while time.monotonic()<deadline:
        found=domain_daemons()
        if len(found)==1 and owned(found[0],str(root)):break
        time.sleep(.1)
    else:raise RuntimeError('one run-owned production daemon did not become ready')
    record=found[0];proc=Path('/proc')/str(record['pid'])
    inodes=set()
    for fd in (proc/'fd').iterdir():
        try: target=os.readlink(fd)
        except FileNotFoundError: continue
        match=re.fullmatch(r'socket:\[(\d+)\]',target)
        if match:inodes.add(match[1])
    udp=[];listeners=[]
    for table in ('udp','udp6','tcp','tcp6'):
        for line in (proc/'net'/table).read_text().splitlines()[1:]:
            fields=line.split()
            if len(fields)<10:raise ValueError('malformed socket table')
            if fields[9] not in inodes:continue
            if table.startswith('udp'):udp.append({'table':table,'local':fields[1]})
            elif fields[3]=='0A':listeners.append({'table':table,'local':fields[1]})
    if udp or listeners != [{'table':'tcp','local':f'0100007F:{11511+175:04X}'}]:
        raise ValueError('daemon has unexpected network sockets')
    record.update(owned_udp=udp,listeners=listeners,
                  library_hashes={p:hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in record['libraries']})
    return record


def main():
    root=Path(sys.argv[1]);run,board,peer,nonce=sys.argv[2:6]
    if (root/'owner').read_text()!=f'MDDS_RUN_OWNER RUN_ID={run} LABEL=ros_broker\n':raise ValueError('wrong owner')
    report={'run_id':run,'board':board,'peer':peer,'nonce':nonce,'results':[],'before':assert_absent()}
    output=root/'cli_daemon';output.mkdir(mode=0o700)
    def command(case,label,args,expected):
        preparation=None
        if case in ('cli:bag/convert','cli:bag/reindex'):
            from bag_transform import prepare
            preparation=prepare(root,expected)
        if case in ('cli:doctor/hello','cli:wtf/hello'):
            from cli_hello import execute as execute_hello
            value=execute_hello(['ros2']+args,output,run,board,case,label,expected,nonce)
        elif case in ('cli:run','cli:launch','cli:test'):
            from cli_process import execute as execute_process
            value=execute_process(['ros2']+args,output,run,board,case,label,expected,nonce)
        elif case in ('cli:topic/hz','cli:topic/bw','cli:topic/delay'):
            from cli_topic_statistics import execute as execute_statistics
            value=execute_statistics(['ros2']+args,output,run,board,case,label,expected,nonce)
        elif case=='cli:service/echo':value=execute_echo(['ros2']+args,output,run,board,case,label,expected,nonce)
        elif case=='cli:component/standalone':value=execute_standalone(['ros2']+args,output,run,board,case,label,expected,nonce)
        elif case=='cli:bag/record':value=execute_record(['ros2']+args,output,run,board,case,label,expected,nonce)
        elif case=='cli:bag/burst':
            from bag_burst import execute as execute_burst
            value=execute_burst(['ros2']+args,output,run,board,case,label,expected,nonce)
        else:value=execute(['ros2']+args,output,run,board,case,label,expected)
        if preparation is not None:
            value['transform_preparation']=preparation
            if value['passed']:
                from bag_transform import inspect
                value['transform_inspection']=inspect(root,expected)
        report['results'].append(value)
        if not value['passed']:
            report['fixture_phase2_started_before_failure']=(root/'phase2.go').exists()
            if case=='cli:node/info':
                report['diagnostic_node_list']=execute(['ros2','node','list','--no-daemon','--spin-time','3'],output,run,board,'cli:node/list','diagnostic_nodes',node_names('/ros_broker_'+run))
            raise RuntimeError('CLI case failed: '+label)
    try:
        command('cli:daemon/status','status_before',['daemon','status'],'The daemon is not running')
        command('cli:daemon/start','start',['daemon','start'],'The daemon has been started')
        report['daemon']=inspect_daemon(root)
        if (root/'cli_batch').read_text().strip()=='daemon_abort':
            (root/'daemon_abort.ready').write_text(json.dumps({'run_id':run,'nonce':nonce,'board':board,'worker_pid':os.getpid(),'worker_start':process_start(os.getpid()),'daemon':report['daemon']})+'\n')
            while True:time.sleep(1)  # The failure-injection driver kills this exact worker.
        command('cli:daemon/status','status_running',['daemon','status'],'The daemon is running')
        # The source fixture is already held live. Give the newly spawned cache
        # time to receive complete remote announcements before its first query.
        time.sleep(3)
        expected=node_names('/ros_broker_'+run)
        if (root/'cli_batch').read_text().strip()=='components':expected+=containers('/ros_broker_'+run)
        command('cli:node/list','nodes_cached',['node','list'],expected)
        command('cli:node/list','nodes_direct',['node','list','--no-daemon','--spin-time','3'],expected)
        if (root/'cli_batch').read_text().strip()=='standalone':
            with (root/'standalone.ready').open('x') as marker:marker.write(nonce+'\n')
            deadline=time.monotonic()+20
            while time.monotonic()<deadline and not (root/'standalone.start').exists():time.sleep(.1)
            if (root/'standalone.start').read_text().strip()!=nonce:raise ValueError('standalone start barrier mismatch')
            report['standalone_start_nonce']=nonce
        if (root/'cli_batch').read_text().strip() in ('process_run','process_launch','process_test'):
            with (root/'process.ready').open('x') as marker:marker.write(nonce+'\n')
            deadline=time.monotonic()+20
            while time.monotonic()<deadline and not (root/'process.start').exists():time.sleep(.1)
            if (root/'process.start').read_text().strip()!=nonce:raise ValueError('process start barrier differs')
            report['process_start_nonce']=nonce
        peer_role='B' if board==acceptance.TARGET['board_serials'][0] else 'A'
        if (root/'cli_batch').read_text().strip()=='endpoint_qos':
            path=root/'endpoint_qos.json';deadline=time.monotonic()+30
            while not path.exists() and time.monotonic()<deadline:time.sleep(.05)
            report['endpoint_qos']=json.loads(path.read_bytes())
        if (root/'cli_batch').read_text().strip()=='graph_late':
            from cli_late_graph import execute as execute_late
            report['late_graph']=execute_late(root,run,board,nonce)
        if (root/'cli_batch').read_text().strip() in ('graph_waiters','graph_remote'):
            from cli_graph_waiters import execute as execute_waiters
            report['graph_waiters']=execute_waiters(root,run,board,nonce)
        if (root/'cli_batch').read_text().strip()=='service_qos':
            from service_qos_contract import validate
            path=root/'service_qos.json';deadline=time.monotonic()+20
            while not path.exists() and time.monotonic()<deadline:time.sleep(.1)
            report['service_qos']=json.loads(path.read_bytes())
            validate(report['service_qos'],run,nonce,'A' if peer_role=='B' else 'B')
        if (root/'cli_batch').read_text().strip()=='trace_probe':
            from board_trace_probe import execute as trace_execute
            report['trace_probe']=trace_execute(root,run,'A' if peer_role=='B' else 'B',nonce)
            report['results'].extend(report['trace_probe']['results'])
        if (root/'cli_batch').read_text().strip() in ('bags','bag_transform','bag_burst'):
            (root/'bags').mkdir()
            (root/'mcap_config.yaml').write_text('noChunking: true\n')
        if (root/'cli_batch').read_text().strip()=='parameter_write':
            import yaml
            (output/'parameter_load.yaml').write_text(yaml.safe_dump({'/ros_broker_'+run+'/alpha_'+peer_role:{'ros__parameters':loaded_values(nonce,peer_role)}}))
        recipes=batch_recipe((root/'cli_batch').read_text().strip(),'/ros_broker_'+run,peer_role,nonce)
        if (root/'cli_batch').read_text().strip()=='trace_probe':recipes=[]
        if (root/'cli_batch').read_text().strip()=='multicast':
            from cli_multicast import run_pair
            values=run_pair(output,run,board,peer_role,nonce);report['results'].extend(values)
            if len(values)!=2 or not all(v['passed'] for v in values):raise RuntimeError('multicast pair failed')
            recipes=[]
        for case,label,argv,wanted in recipes:
            command(case,label,argv,wanted)
            if case in ('cli:doctor/hello','cli:wtf/hello'):
                marker=root/(label+'_gone.json');deadline=time.monotonic()+12
                while time.monotonic()<deadline and not marker.exists():time.sleep(.05)
                if json.loads(marker.read_text())['nonce']!=nonce:raise ValueError('hello graph withdrawal differs')
            if case in ('cli:run','cli:launch','cli:test'):
                gone_path=root/(wanted.get('artifact_prefix','process')+'_gone.json')
                deadline=time.monotonic()+12
                while time.monotonic()<deadline and not gone_path.exists():time.sleep(.1)
                proof=json.loads(gone_path.read_text())
                if proof['run_id']!=run or proof['nonce']!=nonce:raise ValueError('process withdrawal identity differs')
                if case=='cli:launch':
                    secondary=root/'secondary_process_gone.json';deadline=time.monotonic()+12
                    while time.monotonic()<deadline and not secondary.exists():time.sleep(.1)
                    proof=json.loads(secondary.read_bytes())
                    if proof['run_id']!=run or proof['nonce']!=nonce or proof['kind']!='launch_secondary':raise ValueError('secondary launch withdrawal differs')
            if case=='cli:bag/play':
                marker=root/('bag_'+wanted['storage']+'_played.json');deadline=time.monotonic()+12
                while time.monotonic()<deadline and not marker.exists():time.sleep(.1)
                if json.loads(marker.read_text())['nonce']!=nonce:raise ValueError('bag replay proof identity differs')
            if case=='cli:component/standalone':
                deadline=time.monotonic()+12
                while time.monotonic()<deadline and not (root/'standalone_gone.json').exists():time.sleep(.1)
                proof=json.loads((root/'standalone_gone.json').read_text())
                if proof['run_id']!=run or proof['nonce']!=nonce:raise ValueError('standalone withdrawal identity mismatch')
            if (root/'cli_batch').read_text().strip()=='components':
                stage={'component_load_survivor':'loaded','component_unload_primary':'retired','component_unload_survivor':'empty'}.get(label)
                if stage:
                    deadline=time.monotonic()+12
                    while time.monotonic()<deadline and not (root/('components_'+stage+'.json')).exists():time.sleep(.1)
                    proof=json.loads((root/('components_'+stage+'.json')).read_text())
                    if proof['run_id']!=run or proof['nonce']!=nonce:raise ValueError('component proof identity differs')
                    if stage=='loaded':
                        from component_process import inspect
                        report['container']=inspect(root,run)
        if (root/'cli_batch').read_text().strip() in ('bags','bag_transform','bag_burst'):report['bag_files']=freeze_bag_files(root)
        report['daemon_after_queries']=inspect_daemon(root)
        if report['daemon_after_queries']['pid']!=report['daemon']['pid'] or report['daemon_after_queries']['start']!=report['daemon']['start']:
            raise ValueError('daemon replaced during graph comparison')
        command('cli:daemon/stop','stop',['daemon','stop'],'The daemon has been stopped')
        deadline=time.monotonic()+10
        while time.monotonic()<deadline:
            try:
                current=observe(report['daemon']['pid'])
                if current['start']!=report['daemon']['start'] or current['state']=='Z':break
            except (FileNotFoundError,ProcessLookupError):break
            time.sleep(.1)
        else:raise RuntimeError('CLI stop left its daemon process live')
        report['daemon_exited']={'pid':report['daemon']['pid'],'start':report['daemon']['start'],'terminated':True}
        report['after_stop']=assert_absent()
        command('cli:daemon/status','status_after',['daemon','status'],'The daemon is not running')
        command('cli:node/list','nodes_after_stop',['node','list','--no-daemon','--spin-time','3'],expected)
        report['after']=assert_absent()
        report['passed']=True
    finally:
        for daemon in domain_daemons():
            if owned(daemon,str(root)):
                report['emergency_cleanup']=retire(daemon['pid'],root,daemon['start'])
        (output/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        print('CLI_DAEMON_RESULT '+json.dumps(report),flush=True)
    return 0


if __name__ == '__main__':raise SystemExit(main())
