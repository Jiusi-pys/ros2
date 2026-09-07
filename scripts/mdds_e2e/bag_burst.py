"""Bounded real CLI burst with an explicit two-board receiver stop barrier."""
import json
import os
import signal
import subprocess
import sys
import time
import yaml
import cli_acceptance as a
from bag_contract import FORMATS,topic,payloads
from bag_record import inspect_process
from board_graph_ownership import process_start
from cli_bag import recipe as recording_recipe


def check_proof(value,run,nonce,board,storage):
    expected={'run_id':run,'nonce':nonce,'board':board,'storage':storage,'stage':'burst','received':payloads(run,nonce,board,storage,'main')[:3]}
    if value!=expected:raise ValueError('burst requires exactly three ordered peer samples')


def recipe(ns,peer,nonce):
    cases=recording_recipe(ns,peer,nonce);run=ns.removeprefix('/ros_broker_');role='A' if peer=='B' else 'B';root='/data/local/tmp/ros2/.mdds-owned-runs/'+run
    for storage in FORMATS:
        original=topic(run,peer,storage,'main');destination=topic(run,role,storage,'burst')
        cases.append(('cli:bag/burst','bag_burst_'+storage,['bag','burst',root+'/bags/'+storage,'--storage',storage,'--topics',original,'--remap',original+':='+destination,'--num-messages','3','--qos-profile-overrides-path',root+'/bag_burst_'+storage+'.yaml'],{'storage':storage,'source_topic':original,'destination_topic':destination,'count':3}))
    return cases


def qos_options(original):
    return {original:{'history':'keep_last','depth':32,'reliability':'reliable','durability':'transient_local'}}


def execute(argv,output,run,board,case,label,expected,nonce):
    root=output.parent;storage=expected['storage'];native=None;stop=None;emergency=False
    options=root/(label+'.yaml')
    with options.open('x') as out:yaml.safe_dump(qos_options(expected['source_topic']),out)
    actual=[sys.executable,'-u','-B','-c','from ros2cli.cli import main; raise SystemExit(main())']+argv[1:]
    outpath=output/(label+'.stdout');errpath=output/(label+'.stderr');marker=root/('bag_'+storage+'.burst_stop')
    with outpath.open('wb') as out,errpath.open('wb') as err,subprocess.Popen(actual,stdout=out,stderr=err,start_new_session=True) as child:
        start=process_start(child.pid)
        def interrupt(sig,frame):
            if child.poll() is None:os.killpg(child.pid,signal.SIGKILL)
            child.wait();raise SystemExit(128+sig)
        previous={s:signal.signal(s,interrupt) for s in (signal.SIGINT,signal.SIGTERM)}
        try:
            deadline=time.monotonic()+20
            while time.monotonic()<deadline and child.poll() is None:
                if marker.exists():
                    if marker.read_text().strip()!=nonce:raise ValueError('burst stop nonce differs')
                    check_proof(json.loads((root/('bag_'+storage+'_burst.json')).read_text()),run,nonce,board,storage)
                    native=inspect_process(child.pid,root,storage)
                    if process_start(child.pid)!=start:raise ValueError('burst player PID reused')
                    stop={'signal':15,'pid':child.pid,'start':start,'nonce':nonce};child.send_signal(signal.SIGTERM);break
                time.sleep(.1)
            try:child.wait(timeout=8)
            except subprocess.TimeoutExpired:emergency=True;os.killpg(child.pid,signal.SIGKILL);child.wait()
        finally:
            if child.poll() is None:emergency=True;os.killpg(child.pid,signal.SIGKILL);child.wait()
            for sig,handler in previous.items():signal.signal(sig,handler)
    stdout=outpath.read_text();stderr=errpath.read_text()
    passed=child.returncode==0 and native is not None and stop is not None and not emergency and 'dsoftbus(local=AF_UNIX physical=dsoftbus_broker' in stderr
    detail={'native':native,'stop':stop,'emergency_cleanup':emergency,'qos_sha256':a.digest(options.read_bytes())}
    execution={'argv':argv,'actual_argv':actual,'board_serial':board,'child_pid':child.pid,'child_start':start,'returncode':child.returncode,'burst_process':detail}
    raw='MDDS_CLI_ACTUAL_ARGV '+json.dumps(actual)+'\nMDDS_CLI_STDOUT_BEGIN\n'+stdout+'\nMDDS_CLI_STDOUT_END\nMDDS_CLI_STDERR_BEGIN\n'+stderr+'\nMDDS_CLI_STDERR_END\nMDDS_BAG_BURST_PROCESS '+json.dumps(detail)+'\n'+a.terminal_marker(run,case,child.returncode,argv,board)+'\n'
    if passed:raw+='MDDS_CLI_FUNCTIONAL CASE='+case+' RESULT=PASS\n'
    log=output/(label+'.log');log.write_text(raw);execution['log']={'path':log.name,'sha256':a.digest(log.read_bytes())}
    return {'case_id':case,'label':label,'expected':expected,'passed':passed,'execution':execution}
