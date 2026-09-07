"""Own two sequential peer processes and preserve their distinct exit records."""
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time
from abrupt_graph_contract import write_json
from abrupt_victim_owner import owned_victim
from cli_daemon_guard import observe
from board_graph_ownership import process_start

root=Path(sys.argv[1]);run,role,nonce=sys.argv[2:5]
if root!=Path('/data/local/tmp/ros2/.mdds-owned-runs')/run or (root/'peer_restart.enabled').read_text().strip()!=nonce:raise ValueError('wrong peer worker root')
child=None;start=None;argv=None
def stop_owned():
    if child is None or child.poll() is not None:return
    value=observe(child.pid)
    if value['start']!=start or value['argv']!=argv or value['broker_root']!=str(root/'brokers'):raise RuntimeError('refusing changed peer child identity')
    os.kill(child.pid,signal.SIGKILL);child.wait(timeout=5)
def interrupted(sig,frame):stop_owned();raise SystemExit(128+sig)
for sig in (signal.SIGINT,signal.SIGTERM):signal.signal(sig,interrupted)
def wait_file(name,allow_exited=False,seconds=120):
    end=time.monotonic()+seconds
    while not (root/name).exists():
        if (not allow_exited and child.poll() is not None) or time.monotonic()>end:raise RuntimeError('peer worker barrier failed: '+name)
        time.sleep(.02)
try:
    for generation in (1,2):
        if generation==2:wait_file('reconnect.peer_old_gone.json',True)
        argv=[sys.executable,'-u','-B',str(root/'board_restart_peer.py'),str(root),run,role,nonce,str(generation)]
        outpath=root/f'peer_{generation}.stdout';errpath=root/f'peer_{generation}.stderr'
        with outpath.open('wb') as out,errpath.open('wb') as err:
            child=subprocess.Popen(argv,stdout=out,stderr=err,start_new_session=True);start=process_start(child.pid)
            wait_file(f'reconnect.peer_{generation}.ready.json')
            if generation==1:
                wait_file('peer_restart.go')
                if (root/'peer_restart.go').read_text().strip()!=nonce:raise ValueError('wrong restart command')
                value=observe(child.pid)
                if not owned_victim(value,str(root),argv,child.pid,start):raise ValueError('peer child is not owned')
                write_json(root,'peer_kill.json',{'run_id':run,'nonce':nonce,'role':role,'signal':9,'identity':value})
                os.kill(child.pid,signal.SIGKILL);child.wait(timeout=5)
            else:
                wait_file('peer_stop.go');child.wait(timeout=15)
            status={'run_id':run,'nonce':nonce,'role':role,'generation':generation,'pid':child.pid,'start':start,'argv':argv,'returncode':child.returncode}
            expected=-9 if generation==1 else 0
            if child.returncode!=expected:raise RuntimeError('wrong peer generation exit')
        raw='RESTART_PEER_ARGV '+json.dumps(argv)+'\n'+outpath.read_text()+'\n'+errpath.read_text()+'\nRESTART_PEER_EXIT '+json.dumps(status)+'\n'
        (root/f'peer_{generation}.log').write_text(raw);write_json(root,f'reconnect.peer_{generation}.status.json',status)
except BaseException:
    (root/'peer_worker.failed').write_text(nonce+'\n');raise
finally:stop_owned()
