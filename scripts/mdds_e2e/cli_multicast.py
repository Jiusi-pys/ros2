"""Execute real cross-board multicast CLI send/receive as a separate diagnostic."""
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


def recipe(ns,peer,nonce):
    expected={'group':'239.255.'+str(int(nonce[4:6],16))+'.'+str(int(nonce[6:8],16)),
              'port':53000+int(nonce[:4],16)%1000,'interface':'192.168.77.'+('201' if peer=='B' else '202'),
              'peer_ip':'192.168.77.'+('202' if peer=='B' else '201'),'device':'eth1'}
    common=['--group',expected['group'],'--port',str(expected['port']),'--interface',expected['interface']]
    return [('cli:multicast/receive','multicast_receive',['multicast','receive']+common,expected),
            ('cli:multicast/send','multicast_send',['multicast','send']+common+['--ttl','1','--no-loopback'],expected)]


def received_packet(stdout,peer_ip):
    rows=re.findall(r"(?m)^Received from ([0-9.]+):([0-9]+): '([^']*)'$",stdout)
    if len(rows)==1 and rows[0][0]==peer_ip and rows[0][2]=='Hello World!' and 0<int(rows[0][1])<65536:return int(rows[0][1])
    return None


def receiver_ready(pid,expected):
    proc=Path('/proc')/str(pid);inodes=set()
    for fd in (proc/'fd').iterdir():
        try:link=os.readlink(fd)
        except FileNotFoundError:continue
        match=re.fullmatch(r'socket:\[(\d+)\]',link)
        if match:inodes.add(match[1])
    address='00000000:'+format(expected['port'],'04X')
    rows=[line.split() for line in (proc/'net/udp').read_text().splitlines()[1:]]
    if sum(row[1]==address and row[9] in inodes for row in rows)!=1:return None
    wanted=socket.inet_aton(expected['group'])[::-1].hex().upper();device=None;member=False
    for line in (proc/'net/igmp').read_text().splitlines()[1:]:
        match=re.match(r'^\d+\s+(\S+)\s*:',line)
        if match:device=match[1]
        elif device==expected['device'] and line.split() and line.split()[0]==wanted:member=True
    if not member:return None
    return {'pid':pid,'start':process_start(pid),'udp_local':address,'group':expected['group'],'device':expected['device']}


def run_pair(output,run,board,peer_role,nonce):
    from cli_daemon import execute as execute_sender
    rows=recipe('/ros_broker_'+run,peer_role,nonce);case,label,args,expected=rows[0];root=output.parent
    argv=['ros2']+args;actual=[sys.executable,'-u','-B','-c','from ros2cli.cli import main; raise SystemExit(main())']+args
    outpath=output/(label+'.stdout');errpath=output/(label+'.stderr');ready=None;sender=None;emergency=False
    with outpath.open('wb') as out,errpath.open('wb') as err,subprocess.Popen(actual,stdout=out,stderr=err,start_new_session=True) as child:
        start=process_start(child.pid)
        def interrupt(sig,frame):
            if child.poll() is None:os.killpg(child.pid,signal.SIGKILL)
            child.wait();raise SystemExit(128+sig)
        previous={s:signal.signal(s,interrupt) for s in (signal.SIGINT,signal.SIGTERM)}
        try:
            deadline=time.monotonic()+20
            while time.monotonic()<deadline and child.poll() is None:
                if ready is None:
                    ready=receiver_ready(child.pid,expected)
                    if ready:
                        with (root/'multicast.ready').open('x') as marker:marker.write(nonce+'\n')
                if ready and (root/'multicast.send').exists():
                    if (root/'multicast.send').read_text().strip()!=nonce:raise ValueError('multicast barrier differs')
                    send_case,send_label,send_args,send_expected=rows[1]
                    sender=execute_sender(['ros2']+send_args,output,run,board,send_case,send_label,send_expected);break
                time.sleep(.05)
            try:child.wait(timeout=5)
            except subprocess.TimeoutExpired:emergency=True;os.killpg(child.pid,signal.SIGKILL);child.wait()
        finally:
            if child.poll() is None:emergency=True;os.killpg(child.pid,signal.SIGKILL);child.wait()
            for sig,handler in previous.items():signal.signal(sig,handler)
    stdout=outpath.read_text();stderr=errpath.read_text();port=received_packet(stdout,expected['peer_ip'])
    passed=child.returncode==0 and ready is not None and sender is not None and sender['passed'] and port is not None and not emergency
    detail={'ready':ready,'peer_source_port':port,'emergency_cleanup':emergency,'barrier_nonce':nonce}
    execution={'argv':argv,'actual_argv':actual,'child_pid':child.pid,'child_start':start,'board_serial':board,'returncode':child.returncode,'multicast_receiver':detail}
    raw='MDDS_CLI_ACTUAL_ARGV '+json.dumps(actual)+'\nMDDS_CLI_STDOUT_BEGIN\n'+stdout+'\nMDDS_CLI_STDOUT_END\nMDDS_CLI_STDERR_BEGIN\n'+stderr+'\nMDDS_CLI_STDERR_END\nMDDS_MULTICAST_RECEIVER '+json.dumps(detail)+'\n'+a.terminal_marker(run,case,child.returncode,argv,board)+'\n'
    if passed:raw+='MDDS_CLI_FUNCTIONAL CASE='+case+' RESULT=PASS\n'
    log=output/(label+'.log');log.write_text(raw);execution['log']={'path':log.name,'sha256':a.digest(log.read_bytes())}
    result={'case_id':case,'label':label,'expected':expected,'passed':passed,'execution':execution}
    return [result]+([sender] if sender else [])
