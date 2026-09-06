"""Actual standalone CLI, its native child and graceful process-group shutdown."""
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


def recipe(ns,peer):
    role='A' if peer=='B' else 'B';run=ns.removeprefix('/ros_broker_');space='/components_'+run
    container='solo_container_'+role+'_'+run
    expected={'role':role,'namespace':space,'container':container,'node':'solo_'+role,'topic':space+'/'+role+'/solo/out'}
    return [('cli:component/standalone','component_standalone',['component','standalone','composition','composition::Talker','--container-node-name',container,'--node-name','solo_'+role,'--node-namespace',space,'-r','chatter:='+expected['topic']],expected)]


def native_matches(record,root,cli_pid,expected):
    exe=str(root)+'/component_prefix/lib/rclcpp_components/component_container'
    return (type(record.get('pid')) is int and record['pid']>0 and str(record.get('start','')).isdecimal()
            and record.get('parent_pid')==cli_pid and record.get('process_group')==cli_pid
            and record.get('executable')==exe and record.get('argv')==[exe,'--ros-args','-r','__node:='+expected['container']])


def execute(argv,output,run,board,case,label,expected,nonce):
    from component_process import inspect_pid
    root=output.parent
    actual=[sys.executable,'-u','-B','-c','from ros2cli.cli import main; raise SystemExit(main())']+argv[1:]
    outpath=output/(label+'.stdout');errpath=output/(label+'.stderr');native=None;shutdown=None;emergency=False
    with outpath.open('wb') as out,errpath.open('wb') as err,subprocess.Popen(actual,stdout=out,stderr=err,start_new_session=True) as child:
        start=process_start(child.pid)
        def interrupted(signum,frame):
            if child.poll() is None:os.killpg(child.pid,signal.SIGKILL)
            child.wait();raise SystemExit(128+signum)
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
                            info=inspect_pid(root,run,int(proc.name))
                            if not native_matches(info,root,child.pid,expected):raise ValueError('standalone child identity mismatch')
                            native=info;break
                        except (FileNotFoundError,ProcessLookupError):continue
                if native and (root/'standalone.stop').exists():
                    if (root/'standalone.stop').read_text().strip()!=nonce or process_start(child.pid)!=start or process_start(native['pid'])!=native['start']:raise ValueError('standalone stop identity mismatch')
                    shutdown={'signal':2,'cli_pid':child.pid,'cli_start':start,'container_pid':native['pid'],'container_start':native['start'],'barrier_nonce':nonce}
                    os.killpg(child.pid,signal.SIGINT)
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
            stat=(Path('/proc')/str(native['pid'])/'stat').read_text().rsplit(')',1)[1].split()
            gone=stat[19]!=native['start'] or stat[0]=='Z'
        except FileNotFoundError:gone=True
    if native and not gone:
        emergency=True
        if process_start(native['pid'])==native['start']:
            os.kill(native['pid'],signal.SIGKILL)
    stdout=outpath.read_text();stderr=errpath.read_text()
    passed=child.returncode==0 and native is not None and shutdown is not None and gone and not emergency
    execution={'argv':argv,'actual_argv':actual,'board_serial':board,'child_pid':child.pid,'child_start':start,'returncode':child.returncode,
               'standalone':{'native':native,'shutdown':shutdown,'container_gone':gone,'emergency_cleanup':emergency}}
    raw='MDDS_CLI_ACTUAL_ARGV '+json.dumps(actual)+'\nMDDS_CLI_STDOUT_BEGIN\n'+stdout+'\nMDDS_CLI_STDOUT_END\nMDDS_CLI_STDERR_BEGIN\n'+stderr+'\nMDDS_CLI_STDERR_END\n'
    raw+='MDDS_STANDALONE_PROCESS '+json.dumps(execution['standalone'])+'\n'
    raw+=acceptance.terminal_marker(run,case,child.returncode,argv,board)+'\n'
    if passed:raw+='MDDS_CLI_FUNCTIONAL CASE='+case+' RESULT=PASS\n'
    log=output/(label+'.log');log.write_text(raw)
    execution['log']={'path':log.name,'sha256':acceptance.digest(log.read_bytes())}
    return {'case_id':case,'label':label,'expected':expected,'passed':passed,'execution':execution}
