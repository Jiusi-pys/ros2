"""Execute real default/include-hidden CLI views and retain their exact output."""
import json
import os
import signal
import subprocess
import sys
from cli_late_graph import wait_marker
from board_graph_ownership import process_start
from hidden_graph_contract import parse_view
import cli_acceptance as a

CASE='graph:hidden_entities'

def recipes():
    result=[]
    options={'node':'--all','topic':'--include-hidden-topics','service':'--include-hidden-services','action':'--include-hidden-actions'}
    for kind in options:
        for hidden in (False,True):
            args=[kind,'list']+([] if kind=='node' else ['--show-types'])+([options[kind]] if hidden else [])
            result.append((kind,hidden,args))
    return result

def execute(root,run,board,nonce):
    (root/'hidden_cli.ready').write_text(nonce+'\n');wait_marker(root,'hidden_cli.go',nonce)
    results=[]
    for kind,hidden,args in recipes():
        label='hidden_'+kind+('_all' if hidden else '_default');argv=['ros2']+args
        actual=[sys.executable,'-B','-c','from ros2cli.cli import main; raise SystemExit(main())']+args
        outpath=root/(label+'.stdout');errpath=root/(label+'.stderr');emergency=False
        with outpath.open('wb') as out,errpath.open('wb') as err,subprocess.Popen(actual,stdout=out,stderr=err,start_new_session=True) as child:
            start=process_start(child.pid)
            def interrupted(sig,frame):
                if child.poll() is None:os.killpg(child.pid,signal.SIGKILL)
                child.wait();raise SystemExit(128+sig)
            previous={s:signal.signal(s,interrupted) for s in (signal.SIGINT,signal.SIGTERM)}
            try:
                try:child.wait(timeout=15)
                except subprocess.TimeoutExpired:emergency=True;os.killpg(child.pid,signal.SIGKILL);child.wait()
            finally:
                if child.poll() is None:emergency=True;os.killpg(child.pid,signal.SIGKILL);child.wait()
                for sig,handler in previous.items():signal.signal(sig,handler)
        stdout=outpath.read_text();stderr=errpath.read_text()
        if child.returncode!=0 or emergency:raise ValueError('hidden CLI command failed: '+label)
        rows=parse_view(stdout,run,kind,hidden)
        raw='MDDS_GRAPH_ACTUAL_ARGV '+json.dumps(actual)+'\nMDDS_CLI_STDOUT_BEGIN\n'+stdout+'\nMDDS_CLI_STDOUT_END\nMDDS_CLI_STDERR_BEGIN\n'+stderr+'\nMDDS_CLI_STDERR_END\n'
        raw+='HIDDEN_GRAPH_VIEW '+json.dumps({'kind':kind,'hidden':hidden,'rows':rows})+'\n'+a.terminal_marker(run,CASE,child.returncode,argv,board)+'\n'
        log=root/(label+'.log');log.write_text(raw)
        results.append({'kind':kind,'hidden':hidden,'rows':rows,'execution':{'argv':argv,'actual_argv':actual,'board_serial':board,'child_pid':child.pid,'child_start':start,'returncode':child.returncode,'log':{'path':log.name,'sha256':a.digest(log.read_bytes())}}})
    value={'run_id':run,'nonce':nonce,'board':board,'results':results};(root/'hidden_cli.json').write_text(json.dumps(value)+'\n')
    (root/'hidden_cli.done').write_text(nonce+'\n');wait_marker(root,'hidden_source.done',nonce)
    return value
