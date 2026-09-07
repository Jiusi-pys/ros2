"""Bind both isolated domains to their native SDK brokers and exact peer traffic."""
import hashlib
import json
import re
from cli_acceptance import TARGET
from domain_isolation_contract import DOMAINS,payloads,validate_snapshot,validate_counts,validate_data

def validate(value,root,run,board,nonce):
    role='A' if board==TARGET['board_serials'][0] else 'B';peer='B' if role=='A' else 'A';peer_board=next(b for b in TARGET['board_serials'] if b!=board)
    record=value['domain_isolation'];raw=(root/(board+'.ros.log')).read_text().splitlines()
    if any(record.get(k)!=v for k,v in {'run_id':run,'nonce':nonce,'role':role}.items()) or json.loads((root/(board+'.domain_isolation.json')).read_bytes())!=record:raise ValueError('domain result identity differs')
    if raw.count('DOMAIN_ISOLATION_RESULT '+json.dumps(record))!=1:raise ValueError('domain result lacks native evidence')
    if set(record['domains'])!={str(d) for d in DOMAINS} or type(record['observation_ns']) is not int or record['observation_ns']<1_000_000_000:raise ValueError('domain controls/window missing')
    other=json.loads((root/(peer_board+'.domain_isolation.json')).read_bytes());hashes=json.loads((root/'late_graph_hashes.json').read_bytes())['hashes'];prefixes=[]
    for domain in DOMAINS:
        r=record['domains'][str(domain)]
        if r['requested_domain']!=domain or r['actual_domain']!=domain:raise ValueError('actual context domain differs')
        validate_snapshot(r['snapshot'],run,domain,hashes);validate_counts(r['counts']);validate_data(r['received'],run,nonce,domain)
        if r['snapshot']!=other['domains'][str(domain)]['snapshot'] or r['sent']!=payloads(run,nonce,domain,role) or other['domains'][str(domain)]['sent']!=payloads(run,nonce,domain,peer):raise ValueError('same-domain peer graph/sends disagree')
        prefixes.append({tuple(e['gid'][:12]) for e in r['snapshot']['endpoints']})
        operand=int(nonce[:7],16)+domain*10+(1 if role=='A' else 2)
        if r['rpc_sum']!=operand+303:raise ValueError('same-domain peer RPC differs')
        argument=int(nonce[:7],16)+domain*10+(1 if peer=='A' else 2)
        server={'run_id':run,'nonce':nonce,'role':role,'domain':domain,'a':argument,'b':303,'sum':argument+303}
        if r['served']!=server or raw.count('DOMAIN_SERVER '+json.dumps(server))!=1:raise ValueError('domain peer server callback differs')
        for message in r['received']:
            if raw.count('DOMAIN_RX '+json.dumps({'run_id':run,'nonce':nonce,'role':role,'domain':domain,'data':message}))!=1:raise ValueError('domain receiving callback missing or duplicated')
    if prefixes[0]&prefixes[1]:raise ValueError('different domains share participant identities')
    status=json.loads((root/(board+'.domain_daemon.status.json')).read_bytes());inspect=json.loads((root/(board+'.domain_daemon.inspect.json')).read_bytes())
    if status['run_id']!=run or status['role']!='domain_daemon' or status['returncode']!=0 or (inspect['pid'],inspect['start'])!=(status['child_pid'],status['child_start']):raise ValueError('secondary broker did not complete normally')
    owner=f"MDDS_OWNED_PROCESS RUN_ID={run} TAG=domain_daemon_child PID={status['child_pid']} START={status['child_start']}"
    if (root/(board+'.domain_daemon.child.pid')).read_text().strip()!=owner:raise ValueError('secondary broker owner differs')
    argv=inspect['argv'];remote='/data/local/tmp/ros2/.mdds-owned-runs/'+run
    if argv[0]!=remote+'/mdds_broker_daemon' or argv[argv.index('--domain')+1]!='176' or argv[argv.index('--socket')+1]!=remote+'/brokers/d176/b.sock':raise ValueError('secondary broker argv/domain differs')
    if inspect['owned_udp'] or len(inspect['sdk'])!=1 or inspect['binary_sha256']!=hashlib.sha256((root/'mdds_broker_daemon').read_bytes()).hexdigest():raise ValueError('secondary broker native SDK provenance differs')
    lines=(root/(board+'.domain_daemon.log')).read_text().splitlines()
    sockets=[re.fullmatch(r'\[mdds/dsoftbus\] Socket\(name=com\.kaihong\.mdds\.broker\.d176 pkg=com\.kaihong\.mdds\)=(\d+)',line) for line in lines]
    sockets=[m for m in sockets if m]
    if len(sockets)!=1 or lines.count('[mdds/dsoftbus] Listen(fd='+sockets[0][1]+')=0')!=1 or sum(line.startswith('[mdds/dsoftbus] OnBind(') for line in lines)!=1:raise ValueError('secondary domain native Socket/Listen/Bind missing')
    stops=[line for line in lines if line.startswith('MDBC_REMOTE_STOP ')]
    if len(stops)!=1:raise ValueError('secondary broker stop missing')
    fields=dict(part.split('=',1) for part in stops[0].split()[1:] if '=' in part)
    if fields.get('run_id')!='d176' or fields.get('result')!='PASS' or any(fields.get(k)!='0' for k in ('connections','active_ports','remote_links','channels','pending_retirements','queued_bytes','reassembly_bytes')):raise ValueError('secondary broker resources remain')
    if lines.count('GRAPH_PROCESS_EXIT '+json.dumps(status,sort_keys=True))!=1:raise ValueError('secondary broker native exit missing')
    for b in TARGET['board_serials']:
        for name in ('hidden_source.go','hidden_cli.ready','hidden_cli.go','domain_isolation.sent','domain_isolation.observe','hidden_cli.done','hidden_source.stop','hidden_source.done'):
            if (root/(b+'.'+name)).read_text().strip()!=nonce:raise ValueError('domain lifecycle/send barrier differs')
