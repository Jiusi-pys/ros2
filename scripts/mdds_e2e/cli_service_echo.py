"""Run the continuous service echo CLI and stop only after exact events arrive."""
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time
import cli_acceptance as acceptance
from board_graph_ownership import process_start
from cli_service_events import events_match


def recipe(ns,peer,nonce):
    local='A' if peer=='B' else 'B';operand=int(nonce[:7],16)+(100 if local=='A' else 200)
    service=ns+'/'+peer+'/introspect'
    return [('cli:service/echo','service_echo',['service','echo',service,'example_interfaces/srv/AddTwoInts','--qos-reliability','reliable'],
             {'service':service,'a':operand,'b':17,'sum':operand+17})]


def execute(argv,output,run,board,case,label,expected,nonce):
    root=output.parent
    actual=[sys.executable,'-u','-B','-c','from ros2cli.cli import main; raise SystemExit(main())']+argv[1:]
    stdout_path=output/(label+'.stdout');stderr_path=output/(label+'.stderr');stop=None
    with stdout_path.open('wb') as out,stderr_path.open('wb') as err,subprocess.Popen(actual,stdout=out,stderr=err,start_new_session=True) as child:
        start=process_start(child.pid)
        def interrupted(signum,frame):
            if child.poll() is None:os.killpg(child.pid,signal.SIGKILL)
            child.wait();raise SystemExit(128+signum)
        handlers={s:signal.signal(s,interrupted) for s in (signal.SIGINT,signal.SIGTERM)}
        try:
            deadline=time.monotonic()+25;triggered=False
            while time.monotonic()<deadline and child.poll() is None:
                if not triggered and (root/'introspection.ready').exists():
                    assert (root/'introspection.ready').read_text().strip()==nonce
                    time.sleep(2)
                    (root/'introspection.go').write_text(nonce+'\n');triggered=True
                if (root/'introspection.result.json').exists():
                    record=json.loads((root/'introspection.result.json').read_text())
                    stdout=stdout_path.read_text()
                    if all(record.get(k)==v for k,v in expected.items()) and events_match(stdout,record):
                        assert process_start(child.pid)==start
                        stop={'signal':2,'reason':'functional_observed','child_pid':child.pid,'child_start':start,'stdout_sha256':acceptance.digest(stdout.encode())}
                        child.send_signal(signal.SIGINT)
                        break
                time.sleep(.05)
            try:child.wait(timeout=5)
            except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait()
        finally:
            for sig,handler in handlers.items():signal.signal(sig,handler)
    stdout=stdout_path.read_text();stderr=stderr_path.read_text()
    record=json.loads((root/'introspection.result.json').read_text()) if (root/'introspection.result.json').exists() else {}
    passed=(stop is not None and child.returncode in (0,2) and all(record.get(k)==v for k,v in expected.items()) and events_match(stdout,record)
            and stop['stdout_sha256']==acceptance.digest(stdout.encode()) and 'dsoftbus(local=AF_UNIX physical=dsoftbus_broker' in stderr)
    execution={'argv':argv,'actual_argv':actual,'board_serial':board,'child_pid':child.pid,'child_start':start,'returncode':child.returncode}
    if stop is not None:execution['controlled_stop']=stop
    raw='MDDS_CLI_ACTUAL_ARGV '+json.dumps(actual)+'\nMDDS_CLI_STDOUT_BEGIN\n'+stdout+'\nMDDS_CLI_STDOUT_END\nMDDS_CLI_STDERR_BEGIN\n'+stderr+'\nMDDS_CLI_STDERR_END\n'
    if stop is not None:raw+=acceptance.controlled_stop_marker(run,case,execution)+'\n'
    raw+=acceptance.terminal_marker(run,case,child.returncode,argv,board)+'\n'
    if passed:raw+='MDDS_CLI_FUNCTIONAL CASE='+case+' RESULT=PASS\n'
    log=output/(label+'.log');log.write_text(raw)
    execution['log']={'path':log.name,'sha256':acceptance.digest(log.read_bytes())}
    return {'case_id':case,'label':label,'expected':expected,'passed':passed,'execution':execution}
