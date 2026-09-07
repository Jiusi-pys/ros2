"""Actual doctor/wtf hello with independent ROS and diagnostic UDP evidence."""
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import socket
import subprocess
import sys
import time
import cli_acceptance as a
from board_graph_ownership import process_start

COMMANDS=('doctor','wtf')


def recipe(ns,peer,nonce):
    run=ns.removeprefix('/ros_broker_');role='A' if peer=='B' else 'B';cases=[]
    for index,command in enumerate(COMMANDS):
        expected={'command':command,'topic':'/hello_'+run+'/'+command,'host_id':role+'_'+nonce,'peer_id':peer+'_'+nonce,'group':'239.255.77.7','port':50000+int(nonce[:4],16)%1000+index,'interface':'192.168.77.'+('201' if role=='A' else '202')}
        args=[command,'hello','--topic',expected['topic'],'--host-id',expected['host_id'],'--multicast-group',expected['group'],'--multicast-port',str(expected['port']),'--multicast-interface',expected['interface'],'--ttl','1','--emit-period','0.1','--print-period','1']
        cases.append(('cli:'+command+'/hello','hello_'+command,args,expected))
    return cases


def summary(stdout,expected):
    for block in stdout.split('MULTIMACHINE COMMUNICATION SUMMARY\n')[1:]:
        if '-'*60 not in block:continue
        text=block.split('-'*60,1)[0]
        pattern=r'Topic: '+re.escape(expected['topic'])+r', Published Msg Count: (\d+)\nSubscribed from:\n(.*?)Multicast Group/Port: '+re.escape(expected['group'])+'/'+str(expected['port'])+r', Sent Msg Count: (\d+)\nReceived from:\n(.*)'
        match=re.fullmatch(pattern,text,re.S)
        if not match:continue
        ros=re.findall(r'(?m)^\s*'+re.escape(expected['peer_id'])+r'\s+(\d+)\s*$',match[2]);udp=re.findall(r'(?m)^\s*'+re.escape(expected['peer_id'])+r'\s+(\d+)\s*$',match[4])
        if len(ros)==len(udp)==1 and min(int(match[1]),int(match[3]),int(ros[0]),int(udp[0]))>=3:
            return {'published':int(match[1]),'ros_received':int(ros[0]),'udp_sent':int(match[3]),'udp_received':int(udp[0])}
    return None


def inspect(pid,root,expected):
    proc=Path('/proc')/str(pid);wanted={str(root/'lib/libmdds.so'),str(root/'lib/librmw_mdds.so')}
    paths={line.split(None,5)[5] for line in (proc/'maps').read_text().splitlines() if len(line.split(None,5))==6 and Path(line.split(None,5)[5]).name in ('libmdds.so','librmw_mdds.so')}
    if paths!=wanted:raise ValueError('hello middleware library paths differ')
    inodes=set()
    for fd in (proc/'fd').iterdir():
        try:target=os.readlink(fd)
        except FileNotFoundError:continue
        match=re.fullmatch(r'socket:\[(\d+)\]',target)
        if match:inodes.add(match[1])
    udp=[]
    for table in ('udp','udp6'):
        for line in (proc/'net'/table).read_text().splitlines()[1:]:
            fields=line.split()
            if fields[9] in inodes:udp.append({'table':table,'local':fields[1],'remote':fields[2]})
    if len(udp)!=2 or any(row['table']!='udp' or row['remote']!='00000000:0000' for row in udp) or sum(row['local']=='00000000:'+format(expected['port'],'04X') for row in udp)!=1:raise ValueError('hello diagnostic UDP socket inventory differs')
    return {'pid':pid,'start':process_start(pid),'hostname':socket.gethostname(),'hashes':{p:hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in sorted(paths)},'diagnostic_udp':udp}


def execute(argv,output,run,board,case,label,expected,nonce):
    root=output.parent;command=expected['command'];native=None;observed=None;stop=None;emergency=False
    with (root/(label+'.ready')).open('x') as f:f.write(nonce+'\n')
    deadline=time.monotonic()+20
    while time.monotonic()<deadline and not (root/(label+'.start')).exists():time.sleep(.05)
    if (root/(label+'.start')).read_text().strip()!=nonce:raise ValueError('hello start barrier differs')
    actual=[sys.executable,'-u','-B','-c','from ros2cli.cli import main; raise SystemExit(main())']+argv[1:]
    outpath=output/(label+'.stdout');errpath=output/(label+'.stderr')
    with outpath.open('wb') as out,errpath.open('wb') as err,subprocess.Popen(actual,stdout=out,stderr=err,start_new_session=True) as child:
        start=process_start(child.pid)
        def interrupt(sig,frame):
            if child.poll() is None:os.killpg(child.pid,signal.SIGKILL)
            child.wait();raise SystemExit(128+sig)
        handlers={s:signal.signal(s,interrupt) for s in (signal.SIGINT,signal.SIGTERM)}
        try:
            deadline=time.monotonic()+20
            while time.monotonic()<deadline and child.poll() is None:
                stdout=outpath.read_text()
                if observed is None and (root/(label+'_received.json')).exists() and summary(stdout,expected):
                    native=inspect(child.pid,root,expected);observed=stdout
                    with (root/(label+'.observed')).open('x') as f:f.write(nonce+'\n')
                if observed is not None and (root/(label+'.stop')).exists():
                    if (root/(label+'.stop')).read_text().strip()!=nonce or process_start(child.pid)!=start:raise ValueError('hello stop identity differs')
                    stop={'signal':2,'reason':'functional_observed','child_pid':child.pid,'child_start':start,'stdout_sha256':''};child.send_signal(signal.SIGINT);break
                time.sleep(.05)
            try:child.wait(timeout=6)
            except subprocess.TimeoutExpired:emergency=True;os.killpg(child.pid,signal.SIGKILL);child.wait()
        finally:
            if child.poll() is None:emergency=True;os.killpg(child.pid,signal.SIGKILL);child.wait()
            for sig,handler in handlers.items():signal.signal(sig,handler)
    stdout=outpath.read_text();stderr=errpath.read_text()
    if stop:stop['stdout_sha256']=a.digest(stdout.encode())
    passed=stop is not None and child.returncode in (0,2) and not emergency and stdout.startswith(observed) and summary(observed,expected) is not None and 'Traceback' not in stderr and 'Exception in thread' not in stderr and 'dsoftbus(local=AF_UNIX physical=dsoftbus_broker' in stderr
    detail={'native':native,'observed_stdout':observed,'emergency_cleanup':emergency}
    execution={'argv':argv,'actual_argv':actual,'board_serial':board,'child_pid':child.pid,'child_start':start,'returncode':child.returncode,'hello_process':detail}
    if stop:execution['controlled_stop']=stop
    raw='MDDS_CLI_ACTUAL_ARGV '+json.dumps(actual)+'\nMDDS_CLI_STDOUT_BEGIN\n'+stdout+'\nMDDS_CLI_STDOUT_END\nMDDS_CLI_STDERR_BEGIN\n'+stderr+'\nMDDS_CLI_STDERR_END\nMDDS_HELLO_PROCESS '+json.dumps(detail)+'\n'
    if stop:raw+=a.controlled_stop_marker(run,case,execution)+'\n'
    raw+=a.terminal_marker(run,case,child.returncode,argv,board)+'\n'
    if passed:raw+='MDDS_CLI_FUNCTIONAL CASE='+case+' RESULT=PASS\n'
    log=output/(label+'.log');log.write_text(raw);execution['log']={'path':log.name,'sha256':a.digest(log.read_bytes())}
    return {'case_id':case,'label':label,'expected':expected,'passed':passed,'execution':execution}
