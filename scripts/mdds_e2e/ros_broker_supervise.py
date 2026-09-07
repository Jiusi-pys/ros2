from pathlib import Path
import hashlib
import json
import os
import re
import sys
from board_graph_ownership import supervise_command, process_start
root = Path(sys.argv[1])
run, role, self, peer, nonce, variant = sys.argv[2:8]
if root != Path('/data/local/tmp/ros2/.mdds-owned-runs') / run:
    raise ValueError('wrong root')
if (root / 'owner').read_text() != f'MDDS_RUN_OWNER RUN_ID={run} LABEL=ros_broker\n':
    raise ValueError('wrong owner')
if not re.fullmatch('[0-9a-f]{32}', nonce) or variant != 'service':
    raise ValueError('wrong nonce/variant')
if role == 'prepare':
    from broker_local_run import read_rclpy_manifest
    from type_description_lifetime import extract_rclpy
    sha = os.environ['MDDS_RCLPY_MANIFEST_SHA']
    manifest = read_rclpy_manifest(root / 'rclpy_package.json', sha)
    extract_rclpy(root / 'rclpy_overlay.tar', manifest, root / 'python')
    print('BROKER_RCLPY_READY manifest_sha256=' + sha)
    raise SystemExit(0)
if role in ('burst_stop_sqlite3','burst_stop_mcap'):
    storage=role.removeprefix('burst_stop_')
    from bag_burst import check_proof
    check_proof(json.loads((root/('bag_'+storage+'_burst.json')).read_text()),run,nonce,self,storage)
    with (root/('bag_'+storage+'.burst_stop')).open('x') as f:f.write(nonce+'\n')
    raise SystemExit(0)
if role in ('process_start','process_stop'):
    name='process.start' if role=='process_start' else 'process.stop'
    with (root/name).open('x') as f:f.write(nonce+'\n')
    raise SystemExit(0)
if role in ('advance', 'finish', 'withdraw','standalone_stop','standalone_start'):
    name = {'advance': 'phase2.go', 'finish': 'ros.stop', 'withdraw': 'peer_exit.go','standalone_stop':'standalone.stop','standalone_start':'standalone.start'}[role]
    with (root / name).open('x') as f:
        f.write(nonce + '\n')
    raise SystemExit(0)
if role == 'inspect':
    role = 'daemon'
    record = (root / (role + '.child.pid')).read_text().strip()
    m = re.fullmatch('MDDS_OWNED_PROCESS RUN_ID=' + re.escape(run) + ' TAG=daemon_child PID=(\\d+) START=(\\d+)', record)
    if not m or process_start(int(m[1])) != m[2]:
        raise ValueError('wrong owned daemon identity')
    pid = int(m[1])
    proc = Path('/proc') / str(pid)
    mapped = []
    for line in (proc / 'maps').read_text().splitlines():
        fields = line.split(None, 5)
        if len(fields) == 6 and fields[5].startswith('/') and ('libsoftbus_client.z.so' in fields[5]):
            mapped.append(fields[5])
    sdk = sorted(set(mapped))
    inodes = set()
    for fd in (proc / 'fd').iterdir():
        try:
            dest = os.readlink(fd)
        except FileNotFoundError:
            continue
        m = re.fullmatch('socket:\\[(\\d+)\\]', dest)
        if m:
            inodes.add(m[1])
    udp = []
    for table in ('udp', 'udp6'):
        path = proc / 'net' / table
        if path.exists():
            for line in path.read_text().splitlines()[1:]:
                cols = line.split()
                if len(cols) < 10:
                    raise ValueError('malformed UDP inventory')
                if cols[9] in inodes:
                    udp.append(line)
    report = {'run_id': run, 'pid': pid, 'start': process_start(pid), 'binary_sha256': hashlib.sha256((proc / 'exe').read_bytes()).hexdigest(), 'sdk': {p: hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in sdk}, 'owned_udp': udp}
    with (root / 'daemon.inspect.json').open('x') as f:
        json.dump(report, f)
    print('INSPECT_READY')
    raise SystemExit(0)
if role == 'daemon':
    command = [sys.executable, str(root / 'mdds_broker_service.py'), '--root', str(root / 'brokers'), '--domain', '175', '--daemon', str(root / 'mdds_broker_daemon'), '--token-exec', str(root / 'mdds_token_exec')]
elif role == 'ros':
    label = 'A' if self == '3e01ff55454d202020104033bf453b00' else 'B'
    command = [sys.executable, str(root / 'ros_broker_probe.py'), '--root', str(root), '--run-id', run, '--role', label, '--self-serial', self, '--peer-serial', peer, '--nonce', nonce, '--manifest-sha', os.environ['MDDS_RCLPY_MANIFEST_SHA']]
elif role == 'container':
    label = 'A' if self == '3e01ff55454d202020104033bf453b00' else 'B'
    command = [str(root/'component_prefix/lib/rclcpp_components/component_container'),'--ros-args','-r','__node:=container_'+label,'-r','__ns:=/components_'+run]
elif role == 'cli':
    module = 'cli_daemon.py' if (root/'cli_batch').read_text().strip() in ('daemon','action','introspection','parameter_read','parameter_write','lifecycle','components','standalone','bags','bag_transform','bag_burst','statistics','process_run') else 'cli_graph_basic.py'
    command = [sys.executable, str(root / module), str(root), run, self, peer, nonce]
else:
    raise ValueError('bad role')
raise SystemExit(supervise_command(command, root / (role + '.status.json'), run, role, '/ros_broker_' + run, root / (role + '.child.pid')))
