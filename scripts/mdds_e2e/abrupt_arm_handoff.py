"""One checked export and import per board, with no timed file-copy round trips."""
import base64
import hashlib
import json
from pathlib import Path
import sys

def decode(encoded,sha,run,nonce,role):
    if not isinstance(encoded,str) or len(encoded)>4096:raise ValueError('arm payload too large')
    data=base64.b64decode(encoded,validate=True)
    if len(data)>2048 or hashlib.sha256(data).hexdigest()!=sha:raise ValueError('arm bytes/hash differ')
    value=json.loads(data)
    if set(value)!={'run_id','nonce','role','armed_ns'} or any(value[k]!=v for k,v in {'run_id':run,'nonce':nonce,'role':role}.items()) or type(value['armed_ns']) is not int or value['armed_ns']<=0:raise ValueError('arm identity differs')
    return data

def main():
    mode=sys.argv[1]
    if mode=='host':
        import subprocess
        from concurrent.futures import ThreadPoolExecutor
        hdc,remote,destination,run,nonce,a,b=sys.argv[2:9];destination=Path(destination)
        def command(board,args):
            text=". /data/local/tmp/ros2/env.sh || exit 70; python3.12 '"+remote+"/abrupt_arm_handoff.py' "+' '.join("'"+v+"'" for v in args)
            result=subprocess.run([hdc,'-t',board,'shell',text],capture_output=True,timeout=10)
            if result.returncode:raise RuntimeError('arm HDC command failed')
            return result.stdout.decode().strip()
        with ThreadPoolExecutor(max_workers=2) as pool:
            jobs=[pool.submit(command,board,['export',remote,run,nonce,role]) for board,role in ((a,'A'),(b,'B'))]
            records=[json.loads(job.result()) for job in jobs]
            for (board,role),record in zip(((a,'A'),(b,'B')),records):
                data=decode(record['data'],record['sha256'],run,nonce,role)
                with (destination/(board+'.abrupt_armed.initial.json')).open('xb') as stream:stream.write(data)
            jobs=[pool.submit(command,board,['import',remote,run,nonce,role,records[index]['data'],records[index]['sha256']]) for board,role,index in ((a,'A',1),(b,'B',0))]
            for job,record in zip(jobs,(records[1],records[0])):
                if job.result()!='ARM_IMPORTED '+record['sha256']:raise ValueError('arm import readback differs')
        print('ABRUPT_ARM_HANDOFF_VERIFIED')
        return
    root=Path(sys.argv[2]);run,nonce,role=sys.argv[3:6]
    if role not in ('A','B') or root!=Path('/data/local/tmp/ros2/.mdds-owned-runs')/run or (root/'owner').read_text()!=f'MDDS_RUN_OWNER RUN_ID={run} LABEL=ros_broker\n':raise ValueError('unowned arm root')
    if mode=='export':
        path=root/'abrupt_armed.json'
        if path.is_symlink() or path.stat().st_size>2048:raise ValueError('invalid arm source')
        data=path.read_bytes();encoded=base64.b64encode(data).decode();sha=hashlib.sha256(data).hexdigest()
        decode(encoded,sha,run,nonce,role);print(json.dumps({'data':encoded,'sha256':sha}))
    elif mode=='import':
        data=decode(sys.argv[6],sys.argv[7],run,nonce,'B' if role=='A' else 'A')
        target=root/'peer_armed.json';temporary=root/'peer_armed.pending'
        if target.exists() or target.is_symlink():raise ValueError('peer arm already exists')
        with temporary.open('xb') as stream:stream.write(data)
        temporary.replace(target)
        print('ARM_IMPORTED '+hashlib.sha256(target.read_bytes()).hexdigest())
    else:raise ValueError('unknown arm operation')

if __name__=='__main__':main()
