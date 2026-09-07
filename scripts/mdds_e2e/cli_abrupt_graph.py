"""Own one victim child, bind its identity, and record its actual SIGKILL exit."""
import hashlib
import json
import os
import signal
import subprocess
import sys
import time
from cli_late_graph import wait_marker
from board_graph_ownership import process_start
from cli_daemon_guard import observe
from abrupt_victim_owner import owned_victim
from abrupt_graph_contract import write_json
import cli_acceptance as a

def execute(root,run,board,nonce):
    role='A' if board==a.TARGET['board_serials'][0] else 'B'
    (root/'hidden_cli.ready').write_text(nonce+'\n');wait_marker(root,'hidden_source.go',nonce)
    argv=[sys.executable,'-u','-B',str(root/'board_abrupt_victim.py'),str(root),run,role,nonce]
    outpath=root/'victim.stdout';errpath=root/'victim.stderr'
    with outpath.open('wb') as out,errpath.open('wb') as err,subprocess.Popen(argv,stdout=out,stderr=err,start_new_session=True) as child:
        start=process_start(child.pid)
        def stop_owned():
            if child.poll() is not None:return
            current=observe(child.pid)
            if current['start']!=start or current['argv']!=argv or current['broker_root']!=str(root/'brokers'):raise RuntimeError('refusing to stop changed victim identity')
            os.kill(child.pid,signal.SIGKILL);child.wait(timeout=5)
        def interrupted(sig,frame):stop_owned();raise SystemExit(128+sig)
        previous={s:signal.signal(s,interrupted) for s in (signal.SIGINT,signal.SIGTERM)}
        try:
            deadline=time.monotonic()+60
            while not (root/'victim.ready.json').exists():
                if child.poll() is not None or time.monotonic()>deadline:raise RuntimeError('victim did not complete peer traffic')
                time.sleep(.02)
            ready=json.loads((root/'victim.ready.json').read_bytes())
            if ready['pid']!=child.pid or ready['start']!=start or ready['run_id']!=run or ready['nonce']!=nonce or ready['role']!=role:raise ValueError('victim ready identity differs')
            wait_marker(root,'victim_kill.go',nonce)
            own_arm=json.loads((root/'abrupt_armed.json').read_bytes());peer_arm=(root/'peer_armed.json').read_bytes();other=json.loads(peer_arm)
            if any(v.get('run_id')!=run or v.get('nonce')!=nonce for v in (own_arm,other)) or own_arm['role']!=role or other['role']==role:raise ValueError('both observers were not armed')
            identity=observe(child.pid)
            if not owned_victim(identity,str(root),argv,child.pid,start):raise ValueError('refusing to kill unowned victim')
            started=time.monotonic_ns();os.kill(child.pid,signal.SIGKILL);child.wait(timeout=5);completed=time.monotonic_ns()
            value={'run_id':run,'nonce':nonce,'role':role,'pid':child.pid,'start':start,'argv':argv,'identity':identity,'signal':9,'returncode':child.returncode,'started_ns':started,'completed_ns':completed,'peer_armed_sha256':hashlib.sha256(peer_arm).hexdigest()}
            if child.returncode!=-9 or (root/'victim.graceful_shutdown').exists():raise ValueError('victim did not exit by expected SIGKILL')
            raw='MDDS_GRAPH_ACTUAL_ARGV '+json.dumps(argv)+'\n'+outpath.read_text()+'\n'+errpath.read_text()+'\nABRUPT_KILL '+json.dumps(value)+'\n'+a.terminal_marker(run,'graph:abrupt_exit',child.returncode,argv,board)+'\n'
            (root/'victim.log').write_text(raw);write_json(root,'victim.status.json',value)
            wait_marker(root,'abrupt_after.ready',nonce,30)
            (root/'hidden_cli.done').write_text(nonce+'\n');wait_marker(root,'hidden_source.done',nonce)
            return json.loads((root/'abrupt_graph.json').read_bytes())
        finally:
            stop_owned()
            for sig,handler in previous.items():signal.signal(sig,handler)
