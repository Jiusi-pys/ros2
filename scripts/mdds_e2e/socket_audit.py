"""Read the native monotonic socket audit ABI from the current process."""
import ctypes
import os

FIELDS=('abi','pid','total_calls','ipv4_datagram_calls','ipv6_datagram_calls','datagram_successes','failed_calls','in_flight','instrumentation_errors')
class Snapshot(ctypes.Structure):
    _fields_=[(name,ctypes.c_uint64) for name in FIELDS]

def snapshot():
    function=ctypes.CDLL(None,use_errno=True).mdds_socket_audit_snapshot
    function.argtypes=[ctypes.POINTER(Snapshot),ctypes.c_size_t];function.restype=ctypes.c_int
    value=Snapshot()
    if function(ctypes.byref(value),ctypes.sizeof(value))!=0:raise RuntimeError('native socket audit failed')
    result={name:getattr(value,name) for name in FIELDS}
    if result['abi']!=1 or result['pid']!=os.getpid() or result['instrumentation_errors']:raise ValueError('wrong native socket audit ABI or failure')
    return result

def observe(library):
    from pathlib import Path
    import hashlib
    paths={v.split(None,5)[5] for v in Path('/proc/self/maps').read_text().splitlines() if len(v.split(None,5))==6 and 'libmdds_test_socket_audit.so' in v}
    if paths!={str(library)}:raise ValueError('audit mapping escaped frozen run')
    return {'mapped':{str(library):hashlib.sha256(Path(library).read_bytes()).hexdigest()},'snapshot':snapshot()}
