"""Pin a private mount namespace and retire only its owned live processes."""
import json
import os
from pathlib import Path
import select
import signal
import subprocess
import time


def observe(pid):
    proc = Path('/proc') / str(pid)
    try:
        stat = (proc / 'stat').read_text().rsplit(')', 1)[1].split()
        namespace = os.readlink(proc / 'ns/mnt')
        again = (proc / 'stat').read_text().rsplit(')', 1)[1].split()
    except FileNotFoundError: return None
    if stat[19] != again[19] or again[0] == 'Z': return None
    return {'pid': pid, 'start': again[19], 'namespace': namespace}


def members(namespace):
    if namespace == os.readlink('/proc/1/ns/mnt') or namespace == os.readlink('/proc/self/ns/mnt'):
        raise ValueError('refusing to retire a shared namespace')
    result = []
    for proc in Path('/proc').iterdir():
        if not proc.name.isdecimal(): continue
        value = observe(int(proc.name))
        if value and value['namespace'] == namespace: result.append(value)
    return result


def send(record, sig):
    if observe(record['pid']) == record:
        try: os.kill(record['pid'], sig)
        except ProcessLookupError: pass


def retire(namespace):
    signaled = []
    initial = members(namespace)
    for record in initial:
        send(record, signal.SIGTERM); signaled.append({**record, 'signal': int(signal.SIGTERM)})
    deadline = time.monotonic() + 1
    while members(namespace) and time.monotonic() < deadline: time.sleep(.05)
    deadline = time.monotonic() + 4
    remaining = members(namespace)
    while remaining and time.monotonic() < deadline:
        # Rescan to include children forked between the first scan and signal.
        for record in remaining:
            send(record, signal.SIGKILL); signaled.append({**record, 'signal': int(signal.SIGKILL)})
        time.sleep(.05); remaining = members(namespace)
    return initial, signaled, remaining


def run(actual, receipt, timeout=180):
    ready_read, ready_write = os.pipe()
    release_read, release_write = os.pipe()
    pin = None; child = None; previous = {}
    report = {'argv': actual, 'system_namespace': os.readlink('/proc/1/ns/mnt'), 'released': False,
              'completed': False, 'timed_out': False, 'signal': None}
    def interrupted(sig, frame):
        report['signal'] = sig
        raise SystemExit(128 + sig)
    try:
        for sig in (signal.SIGINT, signal.SIGTERM): previous[sig] = signal.signal(sig, interrupted)
        env = dict(os.environ, MDDS_NAMESPACE_READY_FD=str(ready_write), MDDS_NAMESPACE_RELEASE_FD=str(release_read))
        child = subprocess.Popen(actual, env=env, pass_fds=(ready_write, release_read))
        os.close(ready_write); ready_write = None
        os.close(release_read); release_read = None
        report['child_pid'] = child.pid
        raw = b''; deadline = time.monotonic() + 10
        while b'\n' not in raw and time.monotonic() < deadline:
            if not select.select([ready_read], [], [], .1)[0]: continue
            chunk = os.read(ready_read, 1024)
            if not chunk: raise RuntimeError('namespace launcher exited before ownership handshake')
            raw += chunk
            if len(raw) > 4096: raise RuntimeError('namespace identity exceeded bound')
        if not raw.endswith(b'\n'): raise RuntimeError('namespace ownership handshake timed out')
        identity = json.loads(raw)
        if identity.get('pid') != child.pid or observe(child.pid) != identity:
            raise RuntimeError('namespace launcher identity changed')
        if identity['namespace'] in (report['system_namespace'], os.readlink('/proc/self/ns/mnt')):
            raise RuntimeError('namespace launcher did not isolate mounts')
        pin = os.open(f'/proc/{child.pid}/ns/mnt', os.O_RDONLY)
        if os.readlink(f'/proc/self/fd/{pin}') != identity['namespace'] or observe(child.pid) != identity:
            raise RuntimeError('namespace changed while pinning')
        report['owner'] = identity
        os.write(release_write, b'G'); os.close(release_write); release_write = None
        report['released'] = True
        try: child.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            report['timed_out'] = True
            raise
        if child.returncode != 0: raise RuntimeError('private tracing worker failed')
        if members(identity['namespace']): raise RuntimeError('successful tracing worker left namespace descendants')
        report['completed'] = True
    finally:
        # Finish bounded cleanup even if another termination signal arrives.
        for sig in previous: signal.signal(sig, signal.SIG_IGN)
        try:
            if release_write is not None:
                os.close(release_write); release_write = None
            if pin is not None:
                namespace = os.readlink(f'/proc/self/fd/{pin}')
                initial, signaled, remaining = retire(namespace)
                report.update(cleanup_initial=initial, cleanup_signaled=signaled, cleanup_remaining=remaining)
                if remaining: raise RuntimeError('owned tracing namespace still has live descendants')
            elif child is not None and child.poll() is None:
                # No workload can have started: release was never sent.
                child.kill()
            if child is not None:
                child.wait(timeout=5); report['returncode'] = child.returncode
        finally:
            Path(receipt).write_text(json.dumps(report, indent=2) + '\n')
            for fd in (ready_read, ready_write, release_read, release_write, pin):
                if fd is not None: os.close(fd)
            for sig, handler in previous.items(): signal.signal(sig, handler)
    return report
