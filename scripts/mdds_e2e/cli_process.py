"""Real ros2 run child ownership, native mappings and graceful group stop."""
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time
import cli_acceptance as a
from bag_record import inspect_process
from board_graph_ownership import process_start


def recipe(ns,peer):
    role='A' if peer=='B' else 'B';run=ns.removeprefix('/ros_broker_');space='/process_'+run
    expected={'role':role,'namespace':space,'node':'run_'+role,'topic':space+'/'+role+'/out'}
    expected['native_args']=['--ros-args','-r','__node:='+expected['node'],'-r','__ns:='+space,'-r','chatter:='+expected['topic']]
    return [('cli:run','process_run',['run','demo_nodes_cpp','talker']+expected['native_args'],expected)]


def native_matches(record,root,cli_pid,expected):
    exe=str(root)+'/execution_prefix/lib/demo_nodes_cpp/talker'
    return (type(record.get('pid')) is int and record['pid']>0 and str(record.get('start','')).isdecimal()
            and record.get('parent_pid')==cli_pid and record.get('process_group')==cli_pid
            and record.get('executable')==exe and record.get('argv')==[exe]+expected['native_args'])


def inspect(root,run,pid):
    proc=Path('/proc')/str(pid);start=process_start(pid);exe=str(root/'execution_prefix/lib/demo_nodes_cpp/talker')
    if os.readlink(proc/'exe')!=exe:raise ValueError('unexpected ros2 run child executable')
    library=str(root/'lib/libtalker_library.so');deadline=time.monotonic()+5
    wanted={str(root/'lib/libmdds.so'),str(root/'lib/librmw_mdds.so'),library}
    while time.monotonic()<deadline:
        paths={line.split(None,5)[5] for line in (proc/'maps').read_text().splitlines() if len(line.split(None,5))==6 and Path(line.split(None,5)[5]).name in ('libmdds.so','librmw_mdds.so','libtalker_library.so')}
        if paths-wanted:raise ValueError('ros2 run loaded a foreign library')
        if paths==wanted:break
        time.sleep(.05)
    else:raise ValueError('ros2 run native libraries never loaded')
    native=inspect_process(pid,root,None);stat=(proc/'stat').read_text().rsplit(')',1)[1].split()
    if process_start(pid)!=start:raise ValueError('ros2 run child reused during inspection')
    native.update(run_id=run,parent_pid=int(stat[1]),process_group=int(stat[2]),executable=exe,argv=(proc/'cmdline').read_bytes().rstrip(b'\0').decode().split('\0'))
    native['hashes'].update({p:hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in (exe,library)})
    return native


def execute(argv,output,run,board,case,label,expected,nonce):
    root=output.parent;native=None;shutdown=None;emergency=False
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
                    if (root/'process.stop').read_text().strip()!=nonce or process_start(child.pid)!=start or process_start(native['pid'])!=native['start']:raise ValueError('ros2 run stop identity differs')
                    shutdown={'signal':2,'cli_pid':child.pid,'cli_start':start,'native_pid':native['pid'],'native_start':native['start'],'barrier_nonce':nonce}
                    os.killpg(child.pid,signal.SIGINT);break
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
    detail={'native':native,'shutdown':shutdown,'native_gone':gone,'emergency_cleanup':emergency}
    execution={'argv':argv,'actual_argv':actual,'board_serial':board,'child_pid':child.pid,'child_start':start,'returncode':child.returncode,'process':detail}
    raw='MDDS_CLI_ACTUAL_ARGV '+json.dumps(actual)+'\nMDDS_CLI_STDOUT_BEGIN\n'+stdout+'\nMDDS_CLI_STDOUT_END\nMDDS_CLI_STDERR_BEGIN\n'+stderr+'\nMDDS_CLI_STDERR_END\nMDDS_CLI_PROCESS '+json.dumps(detail)+'\n'+a.terminal_marker(run,case,child.returncode,argv,board)+'\n'
    if passed:raw+='MDDS_CLI_FUNCTIONAL CASE='+case+' RESULT=PASS\n'
    log=output/(label+'.log');log.write_text(raw);execution['log']={'path':log.name,'sha256':a.digest(log.read_bytes())}
    return {'case_id':case,'label':label,'expected':expected,'passed':passed,'execution':execution}
