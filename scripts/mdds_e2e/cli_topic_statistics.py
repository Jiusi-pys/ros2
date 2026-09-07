"""Stop the real continuous statistics CLI only after data and valid output."""
import json
import os
import signal
import subprocess
import sys
import time
import cli_acceptance as a
from topic_statistics import oracle
from board_graph_ownership import process_start
from bag_record import inspect_process


def execute(argv,output,run,board,case,label,expected,nonce):
    root=output.parent;verb=expected['verb'];native=None;stop=None;observed=None;emergency=False
    actual=[sys.executable,'-u','-B','-c','from ros2cli.cli import main; raise SystemExit(main())']+argv[1:]
    outpath=output/(label+'.stdout');errpath=output/(label+'.stderr')
    with outpath.open('wb') as out,errpath.open('wb') as err,subprocess.Popen(actual,stdout=out,stderr=err,start_new_session=True) as child:
        start=process_start(child.pid)
        def interrupt(sig,frame):
            if child.poll() is None:os.killpg(child.pid,signal.SIGKILL)
            child.wait();raise SystemExit(128+sig)
        previous={s:signal.signal(s,interrupt) for s in (signal.SIGINT,signal.SIGTERM)}
        try:
            with (root/('stats_'+verb+'.go')).open('x') as marker:marker.write(nonce+'\n')
            deadline=time.monotonic()+20;proof_path=root/('stats_'+verb+'_received.json')
            while time.monotonic()<deadline and child.poll() is None:
                if proof_path.exists():
                    proof=json.loads(proof_path.read_text());stdout=outpath.read_text()
                    if oracle(verb,stdout,proof):
                        native=inspect_process(child.pid,root,None);observed=stdout
                        if process_start(child.pid)!=start:raise ValueError('statistics PID reused')
                        stop={'signal':2,'reason':'functional_observed','child_pid':child.pid,'child_start':start,'stdout_sha256':a.digest(stdout.encode())}
                        child.send_signal(signal.SIGINT);break
                time.sleep(.05)
            try:child.wait(timeout=5)
            except subprocess.TimeoutExpired:emergency=True;os.killpg(child.pid,signal.SIGKILL);child.wait()
        finally:
            if child.poll() is None:emergency=True;os.killpg(child.pid,signal.SIGKILL);child.wait()
            for sig,handler in previous.items():signal.signal(sig,handler)
    stdout=outpath.read_text();stderr=errpath.read_text()
    # Timers may append another valid row while SIGINT is being delivered.
    # Keep the exact pre-signal observation separately and bind the final log.
    if stop is not None:stop['stdout_sha256']=a.digest(stdout.encode())
    passed=stop is not None and child.returncode in (0,2) and not emergency and stdout.startswith(observed) and oracle(verb,observed,proof) and 'dsoftbus(local=AF_UNIX physical=dsoftbus_broker' in stderr
    detail={'native':native,'observed_stdout':observed,'emergency_cleanup':emergency}
    execution={'argv':argv,'actual_argv':actual,'board_serial':board,'child_pid':child.pid,'child_start':start,'returncode':child.returncode,'statistics_process':detail}
    if stop is not None:execution['controlled_stop']=stop
    raw='MDDS_CLI_ACTUAL_ARGV '+json.dumps(actual)+'\nMDDS_CLI_STDOUT_BEGIN\n'+stdout+'\nMDDS_CLI_STDOUT_END\nMDDS_CLI_STDERR_BEGIN\n'+stderr+'\nMDDS_CLI_STDERR_END\nMDDS_STATISTICS_PROCESS '+json.dumps(detail)+'\n'
    if stop is not None:raw+=a.controlled_stop_marker(run,case,execution)+'\n'
    raw+=a.terminal_marker(run,case,child.returncode,argv,board)+'\n'
    if passed:raw+='MDDS_CLI_FUNCTIONAL CASE='+case+' RESULT=PASS\n'
    log=output/(label+'.log');log.write_text(raw);execution['log']={'path':log.name,'sha256':a.digest(log.read_bytes())}
    return {'case_id':case,'label':label,'expected':expected,'passed':passed,'execution':execution}
