"""Real ROS CLI daemon lifecycle and cached/direct graph comparison."""
from collections import Counter
import hashlib
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
from cli_daemon_guard import assert_absent, domain_daemons, observe, owned, retire


def node_names(namespace):
    return [namespace+'/'+name+'_'+role for role in ('A','B') for name in ('alpha','beta','duplicate','duplicate')]


def oracle(case, stdout, expected):
    lines = [line.strip() for line in stdout.splitlines() if line.strip()]
    if case == 'cli:node/list': return Counter(lines) == Counter(expected)
    if case in ('cli:daemon/start','cli:daemon/status','cli:daemon/stop'): return lines == [expected]
    return False


def execute(argv, directory, run, board, case, label, expected):
    actual = [sys.executable,'-B','-c','from ros2cli.cli import main; raise SystemExit(main())']+argv[1:]
    with subprocess.Popen(actual, stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True) as child:
        start = process_start(child.pid)
        def stop(signum, frame):
            if child.poll() is None: os.killpg(child.pid, signal.SIGKILL)
            child.wait()
            raise SystemExit(128+signum)
        previous = {s:signal.signal(s,stop) for s in (signal.SIGINT,signal.SIGTERM)}
        try:
            try: stdout, stderr = child.communicate(timeout=20)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid,signal.SIGKILL)
                stdout, stderr = child.communicate()
        finally:
            for s,handler in previous.items(): signal.signal(s,handler)
        stdout, stderr = stdout.decode(), stderr.decode()
        passed = child.returncode == 0 and oracle(case,stdout,expected)
        if case == 'cli:node/list':
            passed = passed and 'nodes in the graph that share an exact name' in stderr
            if '--no-daemon' in argv:
                passed = passed and 'dsoftbus(local=AF_UNIX physical=dsoftbus_broker' in stderr
        marker = 'MDDS_CLI_FUNCTIONAL CASE='+case+' RESULT=PASS'
        log = directory / (label+'.log')
        text = ('MDDS_CLI_ACTUAL_ARGV '+json.dumps(actual)+'\nMDDS_CLI_STDOUT_BEGIN\n'+stdout+
                '\nMDDS_CLI_STDOUT_END\nMDDS_CLI_STDERR_BEGIN\n'+stderr+'\nMDDS_CLI_STDERR_END\n'+
                acceptance.terminal_marker(run,case,child.returncode,argv,board)+'\n')
        if passed: text += marker+'\n'
        log.write_text(text,encoding='utf-8')
        return {'case_id':case,'label':label,'expected':expected,'passed':passed,'execution':{
            'argv':argv,'actual_argv':actual,'child_pid':child.pid,'child_start':start,'board_serial':board,
            'returncode':child.returncode,'log':{'path':log.name,'sha256':acceptance.digest(log.read_bytes())}}}


def inspect_daemon(root):
    deadline=time.monotonic()+5
    while time.monotonic()<deadline:
        found=domain_daemons()
        if len(found)==1 and owned(found[0],str(root)):break
        time.sleep(.1)
    else:raise RuntimeError('one run-owned production daemon did not become ready')
    record=found[0];proc=Path('/proc')/str(record['pid'])
    inodes=set()
    for fd in (proc/'fd').iterdir():
        try: target=os.readlink(fd)
        except FileNotFoundError: continue
        match=re.fullmatch(r'socket:\[(\d+)\]',target)
        if match:inodes.add(match[1])
    udp=[];listeners=[]
    for table in ('udp','udp6','tcp','tcp6'):
        for line in (proc/'net'/table).read_text().splitlines()[1:]:
            fields=line.split()
            if len(fields)<10:raise ValueError('malformed socket table')
            if fields[9] not in inodes:continue
            if table.startswith('udp'):udp.append({'table':table,'local':fields[1]})
            elif fields[3]=='0A':listeners.append({'table':table,'local':fields[1]})
    if udp or listeners != [{'table':'tcp','local':f'0100007F:{11511+175:04X}'}]:
        raise ValueError('daemon has unexpected network sockets')
    record.update(owned_udp=udp,listeners=listeners,
                  library_hashes={p:hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in record['libraries']})
    return record


def main():
    root=Path(sys.argv[1]);run,board,peer,nonce=sys.argv[2:6]
    if (root/'owner').read_text()!=f'MDDS_RUN_OWNER RUN_ID={run} LABEL=ros_broker\n':raise ValueError('wrong owner')
    report={'run_id':run,'board':board,'peer':peer,'nonce':nonce,'results':[],'before':assert_absent()}
    output=root/'cli_daemon';output.mkdir(mode=0o700)
    def command(case,label,args,expected):
        value=execute(['ros2']+args,output,run,board,case,label,expected)
        report['results'].append(value)
        if not value['passed']:raise RuntimeError('CLI case failed: '+label)
    try:
        command('cli:daemon/status','status_before',['daemon','status'],'The daemon is not running')
        command('cli:daemon/start','start',['daemon','start'],'The daemon has been started')
        report['daemon']=inspect_daemon(root)
        command('cli:daemon/status','status_running',['daemon','status'],'The daemon is running')
        # The source fixture is already held live. Give the newly spawned cache
        # time to receive complete remote announcements before its first query.
        time.sleep(3)
        expected=node_names('/ros_broker_'+run)
        command('cli:node/list','nodes_cached',['node','list'],expected)
        command('cli:node/list','nodes_direct',['node','list','--no-daemon','--spin-time','3'],expected)
        report['daemon_after_queries']=inspect_daemon(root)
        if report['daemon_after_queries']['pid']!=report['daemon']['pid'] or report['daemon_after_queries']['start']!=report['daemon']['start']:
            raise ValueError('daemon replaced during graph comparison')
        command('cli:daemon/stop','stop',['daemon','stop'],'The daemon has been stopped')
        deadline=time.monotonic()+10
        while time.monotonic()<deadline:
            try:
                current=observe(report['daemon']['pid'])
                if current['start']!=report['daemon']['start'] or current['state']=='Z':break
            except (FileNotFoundError,ProcessLookupError):break
            time.sleep(.1)
        else:raise RuntimeError('CLI stop left its daemon process live')
        report['daemon_exited']={'pid':report['daemon']['pid'],'start':report['daemon']['start'],'terminated':True}
        report['after_stop']=assert_absent()
        command('cli:daemon/status','status_after',['daemon','status'],'The daemon is not running')
        command('cli:node/list','nodes_after_stop',['node','list','--no-daemon','--spin-time','3'],expected)
        report['after']=assert_absent()
        report['passed']=True
    finally:
        for daemon in domain_daemons():
            if owned(daemon,str(root)):
                report['emergency_cleanup']=retire(daemon['pid'],root,daemon['start'])
        (output/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        print('CLI_DAEMON_RESULT '+json.dumps(report),flush=True)
    return 0


if __name__ == '__main__':raise SystemExit(main())
