"""Real ros2 run child ownership, native mappings and graceful group stop."""
import hashlib
import json
import os
import re
from pathlib import Path
import signal
import subprocess
import sys
import time
import cli_acceptance as a
from bag_record import inspect_process
from board_graph_ownership import process_start


def recipe(ns,peer,mode='run'):
    if mode not in ('run','launch','test'):raise ValueError('unknown process CLI mode')
    role='A' if peer=='B' else 'B';run=ns.removeprefix('/ros_broker_');space='/process_'+run
    expected={'role':role,'namespace':space,'node':mode+'_'+role,'topic':space+'/'+role+'/out'}
    expected['native_args']=['--ros-args','-r','__node:='+expected['node'],'-r','__ns:='+space,'-r','chatter:='+expected['topic']]
    if mode=='test':
        root='/data/local/tmp/ros2/.mdds-owned-runs/'+run
        path=root+'/execution_prefix/share/mdds_cli_fixture/peer_talker_test.py';expected.update(test_file=path,junit_file=root+'/process_test.junit.xml')
        return [('cli:test','process_test',['test',path,'--package-name','mdds_cli_fixture','--junit-xml',expected['junit_file']],expected)]
    if mode=='launch':
        path='/data/local/tmp/ros2/.mdds-owned-runs/'+run+'/process_talker.launch.py';expected['launch_file']=path
        return [('cli:launch','process_launch',['launch','--noninteractive',path,'node_name:='+expected['node'],'node_namespace:='+space,'output_topic:='+expected['topic']],expected)]
    return [('cli:run','process_run',['run','demo_nodes_cpp','talker']+expected['native_args'],expected)]


def native_matches(record,root,cli_pid,expected):
    exe=str(root)+'/execution_prefix/lib/demo_nodes_cpp/talker'
    return (type(record.get('pid')) is int and record['pid']>0 and str(record.get('start','')).isdecimal()
            and record.get('parent_pid')==cli_pid and record.get('process_group')==cli_pid
            and record.get('executable')==exe and record.get('argv')==[exe]+expected['native_args'])


def native_exit_code(raw,pid):
    outcomes=[0 for _ in re.finditer(r'(?m)^\[INFO\] \[talker-1\]: process has finished cleanly \[pid '+str(pid)+r'\]$',raw)]
    outcomes += [int(m.group(1)) for m in re.finditer(r'(?m)^\[ERROR\] \[talker-1\]: process has died \[pid '+str(pid)+r', exit code (-?[0-9]+), cmd ',raw)]
    return outcomes[0] if len(outcomes)==1 else None


def wait_executable(pid,expected,start):
    deadline=time.monotonic()+5
    while time.monotonic()<deadline:
        if process_start(pid)!=start:raise ValueError('process changed before exec')
        if os.readlink(Path('/proc')/str(pid)/'exe')==expected:return
        time.sleep(.05)
    raise ValueError('expected child executable never became ready')


def native_libraries(root):
    result={str(root/'lib'/name) for name in ('libmdds.so','librmw_mdds.so','libtalker_library.so')}
    if (root/'lib/librclcpp.so').is_file():result.add(str(root/'lib/librclcpp.so'))
    return result


def inspect(root,run,pid):
    proc=Path('/proc')/str(pid);start=process_start(pid);exe=str(root/'execution_prefix/lib/demo_nodes_cpp/talker')
    wait_executable(pid,exe,start)
    deadline=time.monotonic()+5
    wanted=native_libraries(root);names={Path(p).name for p in wanted}
    while time.monotonic()<deadline:
        paths={line.split(None,5)[5] for line in (proc/'maps').read_text().splitlines() if len(line.split(None,5))==6 and Path(line.split(None,5)[5]).name in names}
        if paths-wanted:raise ValueError('ros2 run loaded a foreign library')
        if paths==wanted:break
        time.sleep(.05)
    else:raise ValueError('ros2 run native libraries never loaded')
    native=inspect_process(pid,root,None);stat=(proc/'stat').read_text().rsplit(')',1)[1].split()
    if process_start(pid)!=start:raise ValueError('ros2 run child reused during inspection')
    native.update(run_id=run,parent_pid=int(stat[1]),process_group=int(stat[2]),executable=exe,argv=(proc/'cmdline').read_bytes().rstrip(b'\0').decode().split('\0'))
    native['hashes'].update({p:hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in wanted|{exe}})
    return native


def execute(argv,output,run,board,case,label,expected,nonce):
    root=output.parent;native=None;shutdown=None;emergency=False
    if case=='cli:test':
        with (root/'process_test_config.json').open('x') as config:
            json.dump({'run_id':run,'nonce':nonce,'board':board,'expected':expected},config)
    actual=[sys.executable,'-u','-B','-c','from ros2cli.cli import main; raise SystemExit(main())']+argv[1:]
    outpath=output/(label+'.stdout');errpath=output/(label+'.stderr')
    with outpath.open('wb') as out,errpath.open('wb') as err,subprocess.Popen(actual,stdout=out,stderr=err,start_new_session=True) as child:
        start=process_start(child.pid)
        def interrupted(sig,frame):
            if child.poll() is None:os.killpg(child.pid,signal.SIGKILL)
            child.wait();raise SystemExit(128+sig)
        previous={s:signal.signal(s,interrupted) for s in (signal.SIGINT,signal.SIGTERM)}
        try:
            deadline=time.monotonic()+30
            while time.monotonic()<deadline and child.poll() is None:
                if native is None:
                    for proc in Path('/proc').iterdir():
                        if not proc.name.isdecimal():continue
                        try:
                            stat=(proc/'stat').read_text().rsplit(')',1)[1].split()
                            if int(stat[1])!=child.pid or stat[0]=='Z':continue
                            info=inspect(root,run,int(proc.name))
                            if not native_matches(info,root,child.pid,expected):raise ValueError('ros2 run child identity differs')
                            native=info;break
                        except (FileNotFoundError,ProcessLookupError):continue
                if native and (root/'process.stop').exists():
                    if case=='cli:test':break  # The tests consume the barrier and the framework stops its child.
                    if (root/'process.stop').read_text().strip()!=nonce or process_start(child.pid)!=start or process_start(native['pid'])!=native['start']:raise ValueError('ros2 run stop identity differs')
                    shutdown={'signal':2,'cli_pid':child.pid,'cli_start':start,'native_pid':native['pid'],'native_start':native['start'],'barrier_nonce':nonce}
                    if case=='cli:launch':
                        shutdown['recipient']='cli';child.send_signal(signal.SIGINT)
                    else:os.killpg(child.pid,signal.SIGINT)
                    break
                time.sleep(.1)
            try:child.wait(timeout=8)
            except subprocess.TimeoutExpired:emergency=True;os.killpg(child.pid,signal.SIGKILL);child.wait()
        finally:
            if child.poll() is None:emergency=True;os.killpg(child.pid,signal.SIGKILL);child.wait()
            for sig,handler in previous.items():signal.signal(sig,handler)
    gone=False
    if native:
        try:
            stat=(Path('/proc')/str(native['pid'])/'stat').read_text().rsplit(')',1)[1].split();gone=stat[19]!=native['start'] or stat[0]=='Z'
        except FileNotFoundError:gone=True
    if native and not gone:
        emergency=True
        if process_start(native['pid'])==native['start']:os.kill(native['pid'],signal.SIGKILL)
    stdout=outpath.read_text();stderr=errpath.read_text();passed=child.returncode==0 and native is not None and shutdown is not None and gone and not emergency
    if case=='cli:test' and native and (root/'process.stop').is_file() and (root/'process.stop').read_text().strip()==nonce:
        shutdown={'method':'launch_testing_completion','cli_pid':child.pid,'cli_start':start,'native_pid':native['pid'],'native_start':native['start'],'barrier_nonce':nonce}
        passed=child.returncode==0 and gone and not emergency
    detail={'native':native,'shutdown':shutdown,'native_gone':gone,'emergency_cleanup':emergency}
    if case in ('cli:launch','cli:test'):
        detail['native_returncode']=native_exit_code(stdout+'\n'+stderr,native['pid']) if native else None
        passed=passed and detail['native_returncode']==0
    if case=='cli:test' and passed:
        from cli_test_report import validate_xml
        detail['junit']=validate_xml((root/'process_test.junit.xml').read_text())
        detail['junit_sha256']=a.digest((root/'process_test.junit.xml').read_bytes())
    execution={'argv':argv,'actual_argv':actual,'board_serial':board,'child_pid':child.pid,'child_start':start,'returncode':child.returncode,'process':detail}
    raw='MDDS_CLI_ACTUAL_ARGV '+json.dumps(actual)+'\nMDDS_CLI_STDOUT_BEGIN\n'+stdout+'\nMDDS_CLI_STDOUT_END\nMDDS_CLI_STDERR_BEGIN\n'+stderr+'\nMDDS_CLI_STDERR_END\nMDDS_CLI_PROCESS '+json.dumps(detail)+'\n'+a.terminal_marker(run,case,child.returncode,argv,board)+'\n'
    if passed:raw+='MDDS_CLI_FUNCTIONAL CASE='+case+' RESULT=PASS\n'
    log=output/(label+'.log');log.write_text(raw);execution['log']={'path':log.name,'sha256':a.digest(log.read_bytes())}
    return {'case_id':case,'label':label,'expected':expected,'passed':passed,'execution':execution}
