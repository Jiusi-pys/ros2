"""Run actual rclcpp graph observers and bind their results to native mappings."""
import json
import os
from pathlib import Path
import signal
import subprocess
import time
import cli_acceptance as a
from board_graph_ownership import process_start
from bag_record import inspect_process

PHASES=[kind+'_'+operation for kind in ('publisher','subscription','service','client','node') for operation in ('create','destroy')]
CASE='graph:local_guard'


def output_contract(raw,run,role,nonce):
    rows=[json.loads(line.removeprefix('GRAPH_WAITER_PHASE ')) for line in raw.splitlines() if line.startswith('GRAPH_WAITER_PHASE ')]
    expected=[{'phase':phase,'first':True,'second':True,'snapshot':True} for phase in PHASES]
    if json.dumps(rows,sort_keys=True)!=json.dumps(expected,sort_keys=True):raise ValueError('graph waiter phases differ')
    seed=int(nonce[:7],16)+(1 if role=='A' else 2)
    for marker in ('GRAPH_WAITER_CONTEXT_SHARED true',f'GRAPH_WAITER_RPC {seed} 170017 {seed+170017}',f'GRAPH_WAITERS_PASS {run} {role} {nonce}'):
        if raw.splitlines().count(marker)!=1:raise ValueError('graph waiter context/peer/terminal evidence missing')
    return rows


def execute(root,run,board,nonce):
    role='A' if board==a.TARGET['board_serials'][0] else 'B'
    (root/'graph_waiters.ready').write_text(nonce+'\n')
    deadline=time.monotonic()+45
    while not (root/'graph_waiters.start').exists() and time.monotonic()<deadline:time.sleep(.05)
    if (root/'graph_waiters.start').read_text().strip()!=nonce:raise ValueError('graph waiter start differs')
    argv=[str(root/'graph_waiters'),run,role,nonce];native=None;emergency=False
    outpath=root/'graph_waiters.stdout';errpath=root/'graph_waiters.stderr'
    with outpath.open('wb') as out,errpath.open('wb') as err,subprocess.Popen(argv,stdout=out,stderr=err,start_new_session=True) as child:
        start=process_start(child.pid)
        def interrupted(sig,frame):
            if child.poll() is None:os.killpg(child.pid,signal.SIGKILL)
            child.wait();raise SystemExit(128+sig)
        previous={s:signal.signal(s,interrupted) for s in (signal.SIGINT,signal.SIGTERM)}
        try:
            wanted={str(root/'lib'/name) for name in ('libmdds.so','librmw_mdds.so','librclcpp.so')}
            names={Path(p).name for p in wanted};deadline=time.monotonic()+7
            while child.poll() is None and time.monotonic()<deadline:
                paths={line.split(None,5)[5] for line in Path(f'/proc/{child.pid}/maps').read_text().splitlines() if len(line.split(None,5))==6 and Path(line.split(None,5)[5]).name in names}
                if paths-wanted:raise ValueError('graph waiter loaded foreign libraries')
                if paths==wanted:
                    native=inspect_process(child.pid,root,None)
                    native['executable']=os.readlink(f'/proc/{child.pid}/exe')
                    native['hashes'].update({p:a.digest(Path(p).read_bytes()) for p in wanted|{argv[0]}})
                    break
                time.sleep(.02)
            try:child.wait(timeout=45)
            except subprocess.TimeoutExpired:emergency=True;os.killpg(child.pid,signal.SIGKILL);child.wait()
        finally:
            if child.poll() is None:emergency=True;os.killpg(child.pid,signal.SIGKILL);child.wait()
            for sig,handler in previous.items():signal.signal(sig,handler)
    raw=outpath.read_text()+'\n'+errpath.read_text()
    value={'run_id':run,'board':board,'role':role,'nonce':nonce,'argv':argv,'pid':child.pid,'start':start,'returncode':child.returncode,'native':native,'emergency_cleanup':emergency}
    log=root/'graph_waiters.log'
    log.write_text('MDDS_GRAPH_ACTUAL_ARGV '+json.dumps(argv)+'\n'+raw+'\n'+a.terminal_marker(run,CASE,child.returncode,argv,board)+'\n')
    value['log']={'path':log.name,'sha256':a.digest(log.read_bytes())}
    (root/'graph_waiters.json').write_text(json.dumps(value)+'\n')
    if child.returncode!=0 or native is None or emergency:raise ValueError('native graph waiter failed')
    output_contract(raw,run,role,nonce)
    (root/'graph_waiters.done').write_text(nonce+'\n')
    return value


def validate(value,root,run,board,nonce):
    role='A' if board==a.TARGET['board_serials'][0] else 'B';remote='/data/local/tmp/ros2/.mdds-owned-runs/'+run
    wanted={'run_id':run,'board':board,'role':role,'nonce':nonce,'argv':[remote+'/graph_waiters',run,role,nonce],'returncode':0,'emergency_cleanup':False}
    if any(value.get(k)!=v for k,v in wanted.items()):raise ValueError('graph waiter identity or exit differs')
    if type(value.get('pid')) is not int or value['pid']<=0 or not isinstance(value.get('start'),str) or not value['start'].isdecimal():raise ValueError('graph waiter PID/start missing')
    if json.loads((root/(board+'.graph_waiters.json')).read_bytes())!=value:raise ValueError('graph waiter report differs')
    native=value['native'];hashes={remote+'/lib/'+name:a.digest((root/name).read_bytes()) for name in ('libmdds.so','librmw_mdds.so','librclcpp.so')}
    hashes[remote+'/graph_waiters']=a.digest((root/'graph_waiters').read_bytes())
    if native['pid']!=value['pid'] or native['start']!=value['start'] or native['executable']!=remote+'/graph_waiters' or native['hashes']!=hashes or native['owned_udp']!=[]:raise ValueError('graph waiter native provenance differs')
    for name in ('graph_waiters','graph_waiters.cpp','librclcpp.so'):
        if (root/('inputs_'+board+'.sha256')).read_text().splitlines().count(a.digest((root/name).read_bytes())+'  '+name)!=1:raise ValueError('graph waiter frozen input differs')
    for suffix in ('ready','start','done'):
        if (root/(board+'.graph_waiters.'+suffix)).read_text().strip()!=nonce:raise ValueError('graph waiter barrier differs')
    raw=(root/(board+'.graph_waiters.stdout')).read_text()+'\n'+(root/(board+'.graph_waiters.stderr')).read_text()
    output_contract(raw,run,role,nonce)
    if 'dsoftbus(local=AF_UNIX physical=dsoftbus_broker' not in raw:raise ValueError('graph waiter did not use DSoftBus')
    peer=next(b for b in a.TARGET['board_serials'] if b!=board);seed=int(nonce[:7],16)+(1 if role=='A' else 2)
    expected={'run_id':run,'nonce':nonce,'role':'B' if role=='A' else 'A','requester':role,'a':seed,'b':170017,'sum':seed+170017}
    if json.loads((root/(peer+'.graph_waiters_server.json')).read_bytes())!=expected:raise ValueError('graph waiter peer callback differs')
    if (root/(peer+'.ros.log')).read_text().splitlines().count('GRAPH_WAITER_SERVER '+json.dumps(expected))!=1:raise ValueError('graph waiter peer callback lacks raw log')


def emit_receipt(root,manifest,reports,run,nonce):
    case=next(c for c in manifest['cases'] if c['id']==CASE)
    executions=[]
    for board in a.TARGET['board_serials']:
        value=reports[board]['graph_waiters'];validate(value,root,run,board,nonce)
        if not isinstance(value.get('log'),dict):raise ValueError('local graph native terminal log missing')
        reference={**value['log'],'path':board+'.'+value['log']['path']}
        raw=a.read_artifact(reference,root).decode()
        original=(root/(board+'.graph_waiters.stdout')).read_text()+'\n'+(root/(board+'.graph_waiters.stderr')).read_text()
        expected='MDDS_GRAPH_ACTUAL_ARGV '+json.dumps(value['argv'])+'\n'+original+'\n'+a.terminal_marker(run,CASE,0,value['argv'],board)+'\n'
        if raw!=expected:raise ValueError('local graph raw output/argv/terminal differs')
        executions.append({'argv':value['argv'],'board_serial':board,'returncode':value['returncode'],
                           'child_pid':value['pid'],'child_start':value['start'],'log':reference})
    receipt={'schema_version':1,'run_id':run,'case_id':CASE,'kind':'functional','status':'PASS',
             'board_serials':a.TARGET['board_serials'],'rmw_implementation':'rmw_mdds','transport':'dsoftbus','executions':executions,
             'assertions':[{'id':kind+'_wakes','passed':True,'execution':0,
                            'pattern':'GRAPH_WAITER_PHASE '+json.dumps({'phase':'publisher_'+kind,'first':True,'second':True,'snapshot':True},separators=(',',':'))}
                           for kind in ('create','destroy')]}
    names=['graph_waiters','graph_waiters.cpp','libmdds.so','librmw_mdds.so','librclcpp.so','host_report.json']
    names += [board+'.'+suffix for board in reports for suffix in ('graph_waiters.json','graph_waiters_server.json','graph_waiters.ready','graph_waiters.start','graph_waiters.done','daemon.log','daemon.inspect.json')]
    receipt['graph_provenance']=[{'path':name,'sha256':a.digest((root/name).read_bytes())} for name in names]
    path=root/'graph_local_guard.receipt.json';path.write_text(json.dumps(receipt,indent=2)+'\n')
    reference={'path':path.name,'sha256':a.digest(path.read_bytes())}
    a.validate_receipt(case,reference,manifest,root)
    case.update(status='PASS',evidence=[reference])
    return CASE
