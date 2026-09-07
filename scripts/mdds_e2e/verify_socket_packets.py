"""Prove both ROS directions using identical native SDK packet bytes."""
import hashlib
import json
from pathlib import Path
import re
import sys
import cli_acceptance as a
from sdk_packet_decoder import parse_trace,decode_packet
from verify_ros_broker import validate as validate_baseline
CASES=['transport:a_to_b','transport:b_to_a']

def packet_line(index,record):return f"SDK_PACKET index={index} direction={record['direction']} fd={record['fd']} result={record['result']} hex={record['data'].hex()}"

def check(root):
    run=root.name;nonce=(root/'nonce').read_text().strip();marker=run+'|'+nonce+'|'
    baseline=validate_baseline(root,run,*a.TARGET['board_serials'])
    if not baseline['passed'] or baseline!=json.loads((root/'host_report.json').read_bytes()):raise ValueError('ROS baseline evidence did not pass or changed')
    if json.loads((root/'transport_cases').read_bytes())!=CASES:raise ValueError('transport cases were not declared')
    boards={}
    for board in a.TARGET['board_serials']:
        lines=(root/(board+'.daemon.log')).read_text().splitlines();ros=(root/(board+'.ros.log')).read_text().splitlines()
        data=(root/(board+'.sdk_packets.bin')).read_bytes();records=parse_trace(data,marker)
        summary=[json.loads(line.removeprefix('SDK_PACKET_TRACE_RESULT ')) for line in lines if line.startswith('SDK_PACKET_TRACE_RESULT ')]
        status=json.loads((root/(board+'.daemon.status.json')).read_bytes())
        if summary!=[{'run_id':run,'nonce':nonce,'pid':status['child_pid'],'count':len(records),'bytes':len(data),'failed':False}]:raise ValueError('native capture summary differs')
        fds={int(m[1]) for line in lines if (m:=re.fullmatch(r'\[mdds/dsoftbus\] OnBind\(fd=(\d+) peer=.+\)',line))}
        if len(fds)!=1:raise ValueError('native SDK channel missing or changed')
        socket=[m for line in lines if (m:=re.fullmatch(r'\[mdds/dsoftbus\] Socket\(name=com\.kaihong\.mdds\.broker\.d175 pkg=com\.kaihong\.mdds\)=(\d+)',line))]
        if len(socket)!=1:raise ValueError('native listener Socket missing')
        listen='[mdds/dsoftbus] Listen(fd='+socket[0][1]+')=0'
        if lines.count(listen)!=1:raise ValueError('native Listen did not succeed')
        binds=[line for line in lines if (m:=re.fullmatch(r'\[mdds/dsoftbus\] BindAsync\(fd=(\d+) peer=.+\)=0',line)) and int(m[1]) in fds]
        for index,r in enumerate(records):
            if lines.count(packet_line(index,r))!=1 or r['fd'] not in fds:raise ValueError('SDK bytes not bound to native trace/channel')
            r['decoded']=decode_packet(r['data']);r['index']=index
        fixture=json.loads((root/(board+'.cli_fixture.json')).read_bytes())
        if any(fixture.get(k)!=v for k,v in {'run_id':run,'nonce':nonce,'board':board}.items()) or ros.count('SDK_PUBLISHER_GIDS '+json.dumps(fixture))!=1:raise ValueError('publisher GID observation lacks native evidence')
        for name in ('sdk_trace.enabled','transport_cases'):
            raw=(root/(board+'.'+name)).read_bytes()
            if raw!=(root/name).read_bytes() or (root/('inputs_'+board+'.sha256')).read_text().splitlines().count(a.digest(raw)+'  '+name)!=1:raise ValueError('transport capture was not frozen')
        boards[board]={'records':records,'ros':ros,'daemon':lines,'fixture':fixture,'listen':listen,'binds':binds}
    A,B=a.TARGET['board_serials']
    if boards[A]['fixture']['topics']!=boards[B]['fixture']['topics'] or not any(v['binds'] for v in boards.values()):raise ValueError('peer GID/bind evidence disagrees')
    proofs={}
    for case,sender,receiver,role,other in [(CASES[0],A,B,'A','B'),(CASES[1],B,A,'B','A')]:
        tx=boards[sender];rx=boards[receiver];gid=tx['fixture']['topics']['/ros_broker_'+run+'/'+role+'/alpha/out']['publisher']['GID'].replace('.','')
        rows=[]
        for phase in (1,2):
            for index in range(5):
                payload=f'{run}|{nonce}|{sender}|alpha|{phase}|{index}'
                sent=[r for r in tx['records'] if r['direction']==1 and r['result']==0 and r['decoded']['payload']==payload and r['decoded']['writer']==gid]
                pairs=[(s,r) for s in sent for r in rx['records'] if r['direction']==2 and r['result']==0 and r['data']==s['data']]
                if not pairs:raise ValueError('no exact successful SendBytes/OnBytes pair for '+payload)
                s,r=pairs[0]
                sent_marker='SDK_ROS_TX '+json.dumps({'run_id':run,'nonce':nonce,'role':role,'name':'alpha','payload':payload})
                receive_marker='SDK_ROS_RX '+json.dumps({'run_id':run,'nonce':nonce,'role':other,'name':'alpha','payload':payload})
                if tx['ros'].count(sent_marker)!=1 or rx['ros'].count(receive_marker)!=1:raise ValueError('exact ROS publish/callback missing or duplicated')
                rows.append({**s['decoded'],'send_index':s['index'],'receive_index':r['index'],'send_fd':s['fd'],'receive_fd':r['fd'],
                             'send_marker':packet_line(s['index'],s),'receive_marker':packet_line(r['index'],r),'ros_marker':receive_marker})
        sequences=[r['sequence'] for r in rows]
        if sequences!=sorted(set(sequences)) or len({r['epoch'] for r in rows})!=1:raise ValueError('writer sample identities differ')
        proofs[case]={'sender':sender,'receiver':receiver,'samples':rows}
    return boards,proofs

def emit(root):
    boards,proofs=check(root);run=root.name;manifest=json.loads((root/'cli_acceptance_manifest.json').read_bytes());manifest['run_id']=run
    for case_id,proof in proofs.items():
        case=next(c for c in manifest['cases'] if c['id']==case_id);sender,receiver=proof['sender'],proof['receiver'];executions=[]
        for board,role in ((sender,'ros'),(receiver,'ros'),(sender,'daemon'),(receiver,'daemon')):
            status=json.loads((root/(board+'.'+role+'.status.json')).read_bytes());raw=(root/(board+'.'+role+'.log')).read_text()
            argv_rows=[json.loads(line.removeprefix('MDDS_GRAPH_ACTUAL_ARGV ')) for line in raw.splitlines() if line.startswith('MDDS_GRAPH_ACTUAL_ARGV ')]
            if status['returncode']!=0 or len(argv_rows)!=1:raise ValueError('native transport process did not complete')
            argv=argv_rows[0]
            if raw.splitlines().count(a.terminal_marker(run,case_id,0,argv,board))!=1:raise ValueError('native transport case terminal missing')
            executions.append({'argv':argv,'board_serial':board,'returncode':0,'child_pid':status['child_pid'],'child_start':status['child_start'],
                               'log':{'path':board+'.'+role+'.log','sha256':a.digest(raw.encode())}})
        bind_board=sender if boards[sender]['binds'] else receiver;first=proof['samples'][0]
        assertions=[('socket_listen',2,boards[sender]['listen']),('socket_bind',2 if bind_board==sender else 3,boards[bind_board]['binds'][0]),
                    ('send_bytes',2,first['send_marker']),('on_bytes_exact',3,first['receive_marker']),('ros_payload_exact',1,first['ros_marker'])]
        receipt={'schema_version':1,'run_id':run,'case_id':case_id,'kind':'functional','status':'PASS','board_serials':a.TARGET['board_serials'],
                 'rmw_implementation':'rmw_mdds','transport':'dsoftbus','executions':executions,'assertions':[{'id':key,'passed':True,'execution':index,'pattern':pattern} for key,index,pattern in assertions],
                 'packet_proof':proof,'artifacts':[{'path':name,'sha256':a.digest((root/name).read_bytes())} for name in ['transport_cases','host_report.json','mdds_broker_daemon']+[b+'.'+suffix for b in a.TARGET['board_serials'] for suffix in ('sdk_packets.bin','cli_fixture.json','daemon.inspect.json')]]}
        path=root/(case_id.replace(':','_')+'.receipt.json');path.write_text(json.dumps(receipt,indent=2)+'\n');reference={'path':path.name,'sha256':a.digest(path.read_bytes())}
        a.validate_receipt(case,reference,manifest,root);case.update(status='PASS',evidence=[reference])
    (root/'cli_partial_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    return proofs

if __name__=='__main__':
    proofs=emit(Path(sys.argv[1]));print('SOCKET_PACKET_CASES_PASS '+json.dumps({k:len(v['samples']) for k,v in proofs.items()}))
