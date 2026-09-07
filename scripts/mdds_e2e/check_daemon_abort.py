"""Native failure injection check, with exact-owner recovery after a RED."""
import json
import hashlib
from pathlib import Path
import subprocess
import sys
import cli_acceptance as a
from cli_daemon_guard import owned

HDC=r'C:\Users\17715\AppData\Local\OpenHarmony\Sdk\23\toolchains\hdc.exe'
root=Path(sys.argv[1]).resolve();run=root.name;remote='/data/local/tmp/ros2/.mdds-owned-runs/'+run
recover='--recover' in sys.argv[2:];observed=[];failed=False
if not recover:
    if json.loads((root/'host_exit.json').read_bytes())['bash_exit']!=42:raise ValueError('exact injected runner exit was not preserved')


def command(board,args):
    result=subprocess.run([HDC,'-t',board,'shell',". /data/local/tmp/ros2/env.sh && python3.12 -B '"+remote+"/cli_daemon_guard.py' "+args],capture_output=True,check=True,timeout=20)
    return json.loads(result.stdout.decode('utf-8'))


for board in a.TARGET['board_serials']:
    ready=json.loads((root/(board+'.daemon_abort.ready')).read_bytes())
    status=json.loads((root/(board+'.cli.status.json')).read_bytes())
    if status['returncode']!=-9 or status['child_pid']!=ready['worker_pid'] or status['child_start']!=ready['worker_start']:
        raise ValueError('failure injection did not kill the exact worker')
    daemons=command(board,'inspect');ours=[d for d in daemons if owned(d,remote)]
    observed.append({'board':board,'worker':status,'surviving_owned_daemons':ours})
    if ours:
        failed=True
        if recover:
            for daemon in ours:
                if daemon['pid']!=ready['daemon']['pid'] or daemon['start']!=ready['daemon']['start']:raise ValueError('unexpected owned daemon identity')
                result=command(board,f"retire --pid {daemon['pid']} --root '{remote}' --start {daemon['start']}")
                observed[-1]['red_recovery']=result
    elif not daemons:
        observed[-1]['after']=command(board,'absent')
        if not recover:
            path=root/(board+'.cli_outer_cleanup.json');remote_report=remote+'/cli_outer_cleanup.json'
            subprocess.run([HDC,'-t',board,'file','recv',remote_report,str(path)],capture_output=True,check=True,timeout=20)
            digest=subprocess.run([HDC,'-t',board,'shell','sha256sum '+remote_report],capture_output=True,check=True,timeout=20).stdout.decode().split()[0]
            if hashlib.sha256(path.read_bytes()).hexdigest()!=digest:raise ValueError('cleanup report transfer differs')
            cleanup=json.loads(path.read_bytes())
            if len(cleanup['selected'])!=1 or cleanup['remaining_owned'] or cleanup['foreign_preserved'] or not cleanup['after']['port_bindable']:raise ValueError('outer cleanup did not prove exact completion')
            selected=cleanup['selected'][0]
            if selected['pid']!=ready['daemon']['pid'] or selected['start']!=ready['daemon']['start']:raise ValueError('cleanup retired another daemon')
            observed[-1]['cleanup_report']={'path':path.name,'sha256':digest}
(root/'daemon_abort_verification.json').write_text(json.dumps({'passed':not failed,'observations':observed},indent=2)+'\n')
print(json.dumps({'passed':not failed,'survivors_each_board':[len(v['surviving_owned_daemons']) for v in observed]}))
raise SystemExit(1 if failed else 0)
