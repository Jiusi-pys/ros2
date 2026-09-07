"""Validate all native loader images across the staged Python launcher's exec."""
import json

def validate_lifetime(lines,pid,program,final,*,bootstrap_exec=True):
    records=[v for v in lines if v.startswith('MDDS_SOCKET_AUDIT_')]
    loaded=f'MDDS_SOCKET_AUDIT_LOADED pid={pid} abi=1 exe={program}'
    if not bootstrap_exec:
        if len(records)!=2 or records[0]!=loaded or not records[1].startswith('MDDS_SOCKET_AUDIT_FINAL ') or json.loads(records[1].removeprefix('MDDS_SOCKET_AUDIT_FINAL '))!=final:
            raise ValueError('native single audit image is incomplete')
        return
    if len(records)!=4 or records[0]!=loaded or records[2]!=loaded or not records[1].startswith('MDDS_SOCKET_AUDIT_EXEC ') or not records[3].startswith('MDDS_SOCKET_AUDIT_FINAL '):
        raise ValueError('audit image was not closed by exactly one exec handoff or final exit')
    checkpoint=json.loads(records[1].removeprefix('MDDS_SOCKET_AUDIT_EXEC '))
    zero={k:(v if k in ('abi','pid') else 0) for k,v in final.items()}
    if checkpoint!=zero or json.loads(records[3].removeprefix('MDDS_SOCKET_AUDIT_FINAL '))!=final:
        raise ValueError('bootstrap counters or final counters differ')
