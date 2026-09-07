"""Require actual preload interception and native final counters on both boards."""
import hashlib
import json
from pathlib import Path
import sys
from cli_acceptance import TARGET
from socket_audit_lifetime import validate_lifetime

def check(root):
    run=root.name;nonce=(root/'nonce').read_text().strip();sha=hashlib.sha256((root/'libmdds_test_socket_audit.so').read_bytes()).hexdigest()
    for board in TARGET['board_serials']:
        for mode in ('missing','positive','zero'):
            stem=board+'.'+mode;status=json.loads((root/(stem+'.status.json')).read_bytes());lines=(root/(stem+'.log')).read_text().splitlines()
            code=3 if mode=='missing' else 0
            if status['run_id']!=run or status['role']!=mode or status['returncode']!=code:raise ValueError('wrong native audit control exit')
            if lines.count('GRAPH_PROCESS_EXIT '+json.dumps(status,sort_keys=True))!=1:raise ValueError('native exit record missing')
            if (root/(stem+'.child.pid')).read_text().strip()!=f"MDDS_OWNED_PROCESS RUN_ID={run} TAG={mode}_child PID={status['child_pid']} START={status['child_start']}":raise ValueError('wrong process ownership')
            command=[json.loads(v.removeprefix('SOCKET_AUDIT_COMMAND ')) for v in lines if v.startswith('SOCKET_AUDIT_COMMAND ')]
            if len(command)!=1 or any(command[0].get(k)!=v for k,v in {'run_id':run,'nonce':nonce,'board':board,'mode':mode,'returncode':code}.items()):raise ValueError('wrong audit command identity')
            if mode=='missing':
                expected='SOCKET_AUDIT_MISSING '+json.dumps({'run_id':run,'nonce':nonce,'board':board,'pid':status['child_pid']})
                if lines.count(expected)!=1 or any(v.startswith('MDDS_SOCKET_AUDIT_') for v in lines):raise ValueError('missing audit control was not exercised')
                continue
            value=json.loads((root/(stem+'.result.json')).read_bytes())
            if lines.count('SOCKET_AUDIT_PROBE '+json.dumps(value))!=1:raise ValueError('native audit probe missing')
            identity={'run_id':run,'nonce':nonce,'board':board,'mode':mode,'pid':status['child_pid'],'start':status['child_start'],'library':f'/data/local/tmp/ros2/.mdds-owned-runs/{run}/libmdds_test_socket_audit.so','library_sha256':sha,'argv':command[0]['argv']}
            if any(value.get(k)!=v for k,v in identity.items()):raise ValueError('wrong probe process or library')
            expected_calls=([(2,2|0x80000|0x800)] if mode=='positive' else [])+[(1,1),(1,2),(2,1),(10,1)]+([(10,2)] if mode=='positive' else [])
            if [(v['family'],v['type']) for v in value['calls']]!=expected_calls or any(v['fd']<0 for v in value['calls']):raise ValueError('real socket controls differ')
            expected={'abi':1,'pid':status['child_pid'],'total_calls':len(expected_calls)+1,'ipv4_datagram_calls':int(mode=='positive'),'ipv6_datagram_calls':int(mode=='positive'),'datagram_successes':2*int(mode=='positive'),'failed_calls':1,'in_flight':0,'instrumentation_errors':0}
            before={k:(v if k in ('abi','pid') else 0) for k,v in expected.items()}
            if mode=='positive':before.update(total_calls=1,ipv4_datagram_calls=1,datagram_successes=1)
            if value['after']!=expected or value['before']!=before or (value['failed_result'],value['failed_errno'])!=(-1,97):raise ValueError('audit counters or errno differ')
            validate_lifetime(lines,status['child_pid'],value['argv'][0],expected)
    return {'run_id':run,'passed':True,'boards':TARGET['board_serials'],'library_sha256':sha,'scope':'real libc socket audit positive/zero/missing controls; not the no-UDP-fallback case'}

if __name__=='__main__':
    root=Path(sys.argv[1]);value=check(root);(root/'report.json').write_text(json.dumps(value,indent=2)+'\n');print('SOCKET_AUDIT_CONTROLS_PASS '+json.dumps(value))
