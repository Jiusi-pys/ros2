#!/bin/env python3.12
"""Bounded board supervisor. Only signals process groups it created."""
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

stopping = False
shared_pids = []


def stop_signal(*_):
    global stopping
    stopping = True


def parse_memory(text):
    mem = {}
    for line in text.splitlines():
        k, v = line.split(':', 1)
        try:
            mem[k] = int(v.split()[0])
        except (ValueError, IndexError):
            pass
    return mem


def snapshot():
    mem = parse_memory(Path('/proc/meminfo').read_text())
    net = {}
    for line in Path('/proc/net/dev').read_text().splitlines()[2:]:
        name, fields = line.split(':')
        a = fields.split()
        net[name.strip()] = dict(rx_bytes=int(a[0]), rx_packets=int(a[1]),
                                 tx_bytes=int(a[8]), tx_packets=int(a[9]))
    temperatures = {}
    for path in Path('/sys/class/thermal').glob('thermal_zone*/temp'):
        try:
            temperatures[path.parent.name] = int(path.read_text())
        except (OSError, ValueError):
            pass
    cpu = [int(x) for x in Path('/proc/stat').read_text().splitlines()[0].split()[1:9]]
    return dict(monotonic_ns=time.monotonic_ns(), available_kib=mem.get('MemAvailable', 0),
                system_cpu_ticks=cpu, shared_softbus=[s for pid in shared_pids if (s := proc(pid))],
                net=net, temperature_millic=temperatures)


def proc(pid):
    try:
        raw = Path(f'/proc/{pid}/stat').read_text()
        a = raw[raw.rindex(')')+2:].split()
        # state index 0, pgrp 2, utime 11, stime 12, start 19, rss 21.
        return dict(pid=pid, group=int(a[2]), ticks=int(a[11])+int(a[12]),
                    start=int(a[19]), rss_kib=int(a[21])*os.sysconf('SC_PAGE_SIZE')//1024)
    except (OSError, ValueError, IndexError):
        return None


def terminate(p):
    if p.poll() is None:
        os.killpg(p.pid, signal.SIGTERM)
        try:
            p.wait(timeout=3)
        except subprocess.TimeoutExpired:
            os.killpg(p.pid, signal.SIGKILL)
            p.wait(timeout=3)


def main(root):
    import resource
    for directory in Path('/proc').iterdir():
        if directory.name.isdigit():
            try:
                if (directory/'comm').read_text().strip() == 'softbus_server':
                    shared_pids.append(int(directory.name))
            except OSError:
                pass
    root = Path(root).resolve(strict=True)
    config = json.loads((root/'config.json').read_text())
    signal.signal(signal.SIGTERM, stop_signal)
    signal.signal(signal.SIGINT, stop_signal)
    status = dict(state='running', reason=None, processes=[], clock_ticks=os.sysconf('SC_CLK_TCK'))
    children, logs = [], []
    def launch(item, suffix=''):
        command = list(item['argv'])
        if suffix:
            command[-2] += suffix
        log = open(root/(item['name']+suffix+'.log'), 'w')
        logs.append(log)
        p = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,
                             env={**os.environ, **config['env']}, start_new_session=True)
        children.append((p, item, suffix))
        return p
    started = time.monotonic()
    restarted = False
    try:
        status['before'] = snapshot()
        need = config['bytes']*5//1024+262144
        if status['before']['available_kib'] < need:
            status['reason'] = 'insufficient_memory_preflight'
            return
        with open(root/'resources.jsonl', 'w') as resources:
            for item in config['processes']:
                p = launch(item)
                if item['name'] == 'broker':
                    until = time.monotonic()+15
                    socket = Path(config['env']['MDDS_BROKER_ROOT'])/'d83/b.sock'
                    while p.poll() is None and not socket.exists() and time.monotonic()<until:
                        time.sleep(.05)
                    if p.poll() is not None or not socket.exists():
                        raise RuntimeError('broker did not create domain socket')
                else:
                    ready=Path(item['argv'][-1])
                    until=time.monotonic()+15
                    while p.poll() is None and not ready.exists() and time.monotonic()<until:
                        time.sleep(.02)
                    if not ready.exists():
                        raise RuntimeError('benchmark initialization failed: '+item['name'])
            peak = 0
            while not stopping and time.monotonic()-started < config['wall_seconds']:
                info = snapshot()
                info['processes'] = [s for p, _, _ in children if p.poll() is None and (s := proc(p.pid))]
                # Include all threads in process CPU ticks; broker is a separate listed process.
                info['rss_total_kib'] = sum(x['rss_kib'] for x in info['processes'])
                peak = max(peak, info['rss_total_kib'])
                resources.write(json.dumps(info)+'\n'); resources.flush()
                if info['available_kib'] < 262144 or info['rss_total_kib'] > config['rss_limit_kib']:
                    status['reason'] = 'memory_limit'; break
                if config.get('restart_after') and not restarted and time.monotonic()-started >= config['restart_after']:
                    targets = [(p, item) for p, item, _ in children if item.get('restart')]
                    for p, item in targets:
                        terminate(p); launch(item, '.restart')
                    if targets:
                        status['restart_ns'] = time.monotonic_ns()
                    restarted = True
                apps = [p for p, item, _ in children if item['name'] != 'broker']
                if apps and all(p.poll() is not None for p in apps):
                    status['reason'] = 'children_completed'; break
                if (root/'stop').exists():
                    status['reason'] = 'host_stop'; break
                time.sleep(.25)
            if status['reason'] is None:
                status['reason'] = 'signal' if stopping else 'wall_timeout'
            status['peak_group_rss_kib'] = peak
    except Exception as e:
        status['reason'] = 'supervisor_exception'
        status['error'] = repr(e)
    finally:
        for p, item, suffix in reversed(children):
            terminate(p)
            status['processes'].append(dict(name=item['name']+suffix, pid=p.pid, returncode=p.returncode))
        for log in logs:
            log.close()
        usage = resource.getrusage(resource.RUSAGE_CHILDREN)
        status['child_cpu_seconds'] = usage.ru_utime+usage.ru_stime
        status['largest_child_peak_rss_kib'] = usage.ru_maxrss
        try:
            status['after'] = snapshot()
        except Exception as e:
            status['snapshot_error'] = repr(e)
        status['elapsed_seconds'] = time.monotonic()-started
        status['state'] = 'finished'
        temporary = root/'status.tmp'
        temporary.write_text(json.dumps(status, indent=2))
        temporary.replace(root/'status.json')


if __name__ == '__main__':
    main(sys.argv[1])
