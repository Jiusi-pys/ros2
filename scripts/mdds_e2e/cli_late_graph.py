"""Own the delayed observer process and wait for source cleanup before CLI exit."""
import json
import os
import signal
import subprocess
import sys
import time
import cli_acceptance as a
from board_graph_ownership import process_start

CASES=('graph:late_join','graph:remote_multi_node')


def wait_marker(root,name,nonce,seconds=45):
    deadline=time.monotonic()+seconds
    while not (root/name).exists() and time.monotonic()<deadline:time.sleep(.05)
    if (root/name).read_text().strip()!=nonce:raise ValueError('late graph barrier differs: '+name)


def execute(root,run,board,nonce):
    role='A' if board==a.TARGET['board_serials'][0] else 'B'
    (root/'late_cli.ready').write_text(nonce+'\n');wait_marker(root,'late_observer.go',nonce)
    argv=[sys.executable,'-u','-B',str(root/'board_late_observer.py'),str(root),run,role,nonce]
    outpath=root/'late_observer.stdout';errpath=root/'late_observer.stderr';emergency=False
    with outpath.open('wb') as out,errpath.open('wb') as err,subprocess.Popen(argv,stdout=out,stderr=err,start_new_session=True) as child:
        start=process_start(child.pid)
        def interrupted(sig,frame):
            if child.poll() is None:os.killpg(child.pid,signal.SIGKILL)
            child.wait();raise SystemExit(128+sig)
        previous={s:signal.signal(s,interrupted) for s in (signal.SIGINT,signal.SIGTERM)}
        try:
            try:child.wait(timeout=30)
            except subprocess.TimeoutExpired:emergency=True;os.killpg(child.pid,signal.SIGKILL);child.wait()
        finally:
            if child.poll() is None:emergency=True;os.killpg(child.pid,signal.SIGKILL);child.wait()
            for sig,handler in previous.items():signal.signal(sig,handler)
    raw='MDDS_GRAPH_ACTUAL_ARGV '+json.dumps(argv)+'\n'+outpath.read_text()+'\n'+errpath.read_text()+'\n'
    raw+='\n'.join(a.terminal_marker(run,case,child.returncode,argv,board) for case in CASES)+'\n'
    path=root/'late_observer.log';path.write_text(raw)
    result={'argv':argv,'pid':child.pid,'start':start,'returncode':child.returncode,'emergency_cleanup':emergency,
            'log':{'path':path.name,'sha256':a.digest(path.read_bytes())}}
    (root/'late_observer.status.json').write_text(json.dumps(result)+'\n')
    if child.returncode!=0 or emergency:raise RuntimeError('late observer process failed')
    result['observation']=json.loads((root/'late_observer.json').read_bytes())
    (root/'late_observer.done').write_text(nonce+'\n')
    wait_marker(root,'late_source.done',nonce)
    return result
