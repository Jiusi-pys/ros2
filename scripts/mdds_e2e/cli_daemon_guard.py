"""Inspect and retire only a precisely identified run-owned ROS CLI daemon."""
import argparse
import json
import os
from pathlib import Path
import signal
import socket
import time

DAEMON_ARGS = ['-c', 'from ros2cli.daemon.daemonize import main; main()',
               '--name', 'ros2-daemon', '--ros-domain-id', '175',
               '--rmw-implementation', 'rmw_mdds']


def owned(record, root):
    return (type(record.get('pid')) is int and record['pid'] > 0
            and str(record.get('start', '')).isdecimal()
            and record.get('state') not in ('Z', None)
            and record.get('argv', [])[1:] == DAEMON_ARGS
            and record.get('broker_root') == root+'/brokers'
            and sorted(record.get('libraries', [])) == [root+'/lib/libmdds.so', root+'/lib/librmw_mdds.so'])


def select_owned(records,root):
    return [record for record in records if owned(record,root)]


def observe(pid):
    proc = Path('/proc') / str(pid)
    stat = (proc / 'stat').read_text().rsplit(')', 1)[1].split()
    argv = (proc / 'cmdline').read_bytes().rstrip(b'\0').decode().split('\0')
    environment = dict(item.split('=', 1) for item in (proc / 'environ').read_bytes().decode().split('\0') if '=' in item)
    mapped = set()
    for line in (proc / 'maps').read_text().splitlines():
        parts = line.split(None, 5)
        if len(parts) == 6 and Path(parts[5]).name in ('libmdds.so', 'librmw_mdds.so'):
            mapped.add(parts[5])
    return {'pid':pid, 'start':stat[19], 'state':stat[0], 'argv':argv,
            'broker_root':environment.get('MDDS_BROKER_ROOT'), 'libraries':sorted(mapped)}


def domain_daemons(domain=175):
    found = []
    for proc in Path('/proc').iterdir():
        if not proc.name.isdecimal(): continue
        try:
            argv = (proc/'cmdline').read_bytes().rstrip(b'\0').decode().split('\0')
            if '--name' in argv and argv[argv.index('--name')+1] == 'ros2-daemon' and '--ros-domain-id' in argv and argv[argv.index('--ros-domain-id')+1] == str(domain):
                found.append(observe(int(proc.name)))
        except (FileNotFoundError, ProcessLookupError):
            continue
    return found


def assert_absent(domain=175):
    found = domain_daemons(domain)
    if found: raise ValueError('existing ROS CLI daemon: '+json.dumps(found))
    # The actual CLI chooses loopback TCP port 11511 + ROS_DOMAIN_ID.
    with socket.socket() as probe:
        probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        probe.bind(('127.0.0.1', 11511+domain))
    return {'domain':domain, 'daemons':[], 'port_bindable':True}


def retire(pid, root, expected_start, require_absent=True):
    root = Path(root)
    run = root.name
    if root != Path('/data/local/tmp/ros2/.mdds-owned-runs')/run or (root/'owner').read_text() != f'MDDS_RUN_OWNER RUN_ID={run} LABEL=ros_broker\n':
        raise ValueError('wrong run directory owner')
    before = observe(pid)
    if before['start'] != expected_start: raise ValueError('PID start identity changed')
    if not owned(before, str(root)): raise ValueError('daemon is not owned by this run')
    current=observe(pid)
    if current['start'] != before['start'] or not owned(current,str(root)): raise ValueError('daemon identity changed before signal')
    os.kill(pid, signal.SIGTERM)
    deadline = time.monotonic()+10
    while time.monotonic() < deadline:
        try:
            current = observe(pid)
            if current['start'] != before['start'] or current['state'] == 'Z': break
        except (FileNotFoundError, ProcessLookupError): break
        time.sleep(.1)
    else:
        raise RuntimeError('owned daemon did not exit after SIGTERM')
    return {'before':before, 'signal':'SIGTERM', 'terminated':True,
            'after':assert_absent() if require_absent else {'owned_process_terminated':True}}


def cleanup(root):
    root=Path(root);run=root.name
    if root!=Path('/data/local/tmp/ros2/.mdds-owned-runs')/run or (root/'owner').read_text()!=f'MDDS_RUN_OWNER RUN_ID={run} LABEL=ros_broker\n':raise ValueError('wrong cleanup owner')
    before=domain_daemons();selected=select_owned(before,str(root));retired=[]
    for record in selected:
        try:retired.append(retire(record['pid'],root,record['start'],require_absent=False))
        except (FileNotFoundError,ProcessLookupError):
            retired.append({'before':record,'terminated':True,'already_gone':True})
    remaining=domain_daemons();ours=select_owned(remaining,str(root))
    if ours:raise RuntimeError('owned CLI daemons remain after cleanup')
    result={'run_id':run,'root':str(root),'selected':selected,'retired':retired,'remaining_owned':ours,
            'foreign_preserved':remaining,'after':assert_absent() if not remaining else None}
    temporary=root/'cli_outer_cleanup.pending';temporary.write_text(json.dumps(result,indent=2)+'\n')
    temporary.replace(root/'cli_outer_cleanup.json')
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('operation', choices=('inspect', 'absent', 'retire','cleanup'))
    parser.add_argument('--pid', type=int)
    parser.add_argument('--root')
    parser.add_argument('--start')
    args = parser.parse_args()
    if args.operation == 'inspect': result = domain_daemons()
    elif args.operation == 'absent': result = assert_absent()
    elif args.operation == 'cleanup': result = cleanup(args.root)
    else: result = retire(args.pid, args.root, args.start)
    print(json.dumps(result))


if __name__ == '__main__': main()
