"""Native positive and zero-datagram controls for the preload audit."""
import errno
import hashlib
import json
import os
from pathlib import Path
import socket
import sys
from board_graph_ownership import process_start,supervise_command

root=Path(sys.argv[1]);run,nonce,board,mode=sys.argv[2:6]
if root!=Path('/data/local/tmp/ros2/.mdds-owned-runs')/run or mode not in ('missing','positive','zero'):raise ValueError('wrong audit probe root/mode')
if (root/'owner').read_text()!=f'MDDS_RUN_OWNER RUN_ID={run} LABEL=socket_audit\n' or (root/'nonce').read_text().strip()!=nonce:raise ValueError('unowned audit probe')
library=root/'libmdds_test_socket_audit.so'
if len(sys.argv)==6:
    os.environ.pop('LD_PRELOAD',None)
    if mode!='missing':os.environ['LD_PRELOAD']=str(library)
    command=[sys.executable,__file__,str(root),run,nonce,board,mode,'child']
    result=supervise_command(command,root/(mode+'.status.json'),run,mode,'/socket_audit',root/(mode+'.child.pid'))
    print('SOCKET_AUDIT_COMMAND '+json.dumps({'run_id':run,'nonce':nonce,'board':board,'mode':mode,'argv':command,'returncode':result}),flush=True)
    raise SystemExit(result)
if sys.argv[-1]!='child':raise ValueError('wrong child mode')
# Exercise interception before ctypes opens the main image and queries the ABI.
early=[]
if mode=='positive':
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM|socket.SOCK_CLOEXEC|socket.SOCK_NONBLOCK) as value:
        early.append({'family':int(socket.AF_INET),'type':int(socket.SOCK_DGRAM|socket.SOCK_CLOEXEC|socket.SOCK_NONBLOCK),'fd':value.fileno()})
import ctypes
from socket_audit import snapshot
if mode=='missing':
    try:snapshot()
    except AttributeError:
        print('SOCKET_AUDIT_MISSING '+json.dumps({'run_id':run,'nonce':nonce,'board':board,'pid':os.getpid()}),flush=True);raise SystemExit(3)
    raise RuntimeError('audit unexpectedly present in missing control')
before=snapshot();assert before['total_calls']==len(early) and before['ipv4_datagram_calls']==len(early) and before['datagram_successes']==len(early)
calls=list(early)
entries=[(socket.AF_UNIX,socket.SOCK_STREAM),(socket.AF_UNIX,socket.SOCK_DGRAM),(socket.AF_INET,socket.SOCK_STREAM),(socket.AF_INET6,socket.SOCK_STREAM)]
if mode=='positive':entries.append((socket.AF_INET6,socket.SOCK_DGRAM))
for family,kind in entries:
    with socket.socket(family,kind) as value:
        calls.append({'family':int(family),'type':int(kind),'fd':value.fileno()})
native=ctypes.CDLL(None,use_errno=True);function=native.socket
function.argtypes=[ctypes.c_int,ctypes.c_int,ctypes.c_int];function.restype=ctypes.c_int
ctypes.set_errno(0);result=function(-1,socket.SOCK_STREAM,0);error=ctypes.get_errno()
assert result==-1 and error==errno.EAFNOSUPPORT
after=snapshot();expected=2 if mode=='positive' else 0
assert after['total_calls']==len(calls)+1 and after['failed_calls']==1 and after['in_flight']==0
assert after['ipv4_datagram_calls']==after['ipv6_datagram_calls']==expected//2 and after['datagram_successes']==expected
mapped={v.split(None,5)[5] for v in Path('/proc/self/maps').read_text().splitlines() if len(v.split(None,5))==6 and 'libmdds_test_socket_audit.so' in v}
assert mapped=={str(library)}
record={'run_id':run,'nonce':nonce,'board':board,'mode':mode,'pid':os.getpid(),'start':process_start(os.getpid()),'argv':Path('/proc/self/cmdline').read_bytes().rstrip(b'\0').decode().split('\0'),
        'library':str(library),'library_sha256':hashlib.sha256(library.read_bytes()).hexdigest(),'before':before,'after':after,'calls':calls,'failed_result':result,'failed_errno':error}
with (root/(mode+'.result.json')).open('x') as output:json.dump(record,output)
print('SOCKET_AUDIT_PROBE '+json.dumps(record),flush=True)
