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
if role in ('hello_start_doctor','hello_stop_doctor','hello_start_wtf','hello_stop_wtf'):
    _,operation,command=role.split('_')
    with (root/('hello_'+command+'.'+operation)).open('x') as f:f.write(nonce+'\n')
    raise SystemExit(0)
if role in ('cycle_pause','cycle_resume'):
    if (root/'reconnect.enabled').read_text().strip()!=nonce:raise ValueError('not an owned remote-cycle fixture')
    name='reconnect.pause' if role=='cycle_pause' else 'reconnect.resume'
    with (root/(name+'.pending')).open('x') as f:f.write(nonce+'\n')
    if (root/name).exists():raise ValueError('remote-cycle command already exists')
    (root/(name+'.pending')).replace(root/name)
    raise SystemExit(0)
if role=='multicast_send':
    with (root/'multicast.send').open('x') as f:f.write(nonce+'\n')
    raise SystemExit(0)
if role=='graph_waiters_start':
    with (root/'graph_waiters.start').open('x') as f:f.write(nonce+'\n')
    raise SystemExit(0)
if role in ('hidden_source_go','hidden_cli_go','hidden_source_stop','victim_kill_go'):
    name={'hidden_source_go':'hidden_source.go','hidden_cli_go':'hidden_cli.go','hidden_source_stop':'hidden_source.stop','victim_kill_go':'victim_kill.go'}[role]
    tmp=root/(name+'.pending')
    with tmp.open('x') as f:f.write(nonce+'\n')
    if (root/name).exists():raise ValueError('hidden phase already released')
    tmp.replace(root/name)
    raise SystemExit(0)
if role=='abort_cli_worker':
    import signal
    if (root/'cli_batch').read_text().strip()!='daemon_abort':raise ValueError('not a failure-injection run')
    value=json.loads((root/'daemon_abort.ready').read_bytes());pid=value['worker_pid'];start=value['worker_start']
    if value['run_id']!=run or value['nonce']!=nonce or value['board']!=self or process_start(pid)!=start:raise ValueError('abort worker identity differs')
    expected=[sys.executable,str(root/'cli_daemon.py'),str(root),run,self,peer,nonce]
    if (Path('/proc')/str(pid)/'cmdline').read_bytes().rstrip(b'\0').decode().split('\0')!=expected:raise ValueError('abort worker argv differs')
    os.kill(pid,signal.SIGKILL)
    print('CLI_WORKER_KILLED '+json.dumps({'pid':pid,'start':start,'signal':9}),flush=True)
    raise SystemExit(0)
if role in ('endpoint_qos_go','endpoint_qos_observe'):
    name='endpoint_qos.go' if role=='endpoint_qos_go' else 'endpoint_qos.observe_go'
    tmp=root/(name+'.pending')
    with tmp.open('x') as f:f.write(nonce+'\n')
    if (root/name).exists():raise ValueError('QoS phase already released')
    tmp.replace(root/name)
    raise SystemExit(0)
if role in ('late_source_go','late_observer_go','late_source_stop'):
    name={'late_source_go':'late_source.go','late_observer_go':'late_observer.go','late_source_stop':'late_source.stop'}[role]
    if (root/name).exists():raise ValueError('late graph phase already released')
    tmp=root/(name+'.pending')
    with tmp.open('x') as f:f.write(nonce+'\n')
    tmp.replace(root/name)
    raise SystemExit(0)
if role.startswith(('remote_change_','remote_applied_')):
    operation,index=role.rsplit('_',1)
    if not index.isdecimal() or not 0<=int(index)<10:raise ValueError('invalid remote phase')
    name=role+('.go' if operation=='remote_change' else '.done')
    if (root/name).exists():raise ValueError('remote phase already signaled')
    tmp=root/(name+'.pending')
    with tmp.open('x') as f:f.write(nonce+'\n')
    tmp.replace(root/name)
    raise SystemExit(0)
if role in ('process_start','process_stop','python_process_stop'):
    name={'process_start':'process.start','process_stop':'process.stop','python_process_stop':'python_process.stop'}[role]
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
    if (root/'reconnect.enabled').exists():
        if (root/'reconnect.enabled').read_text().strip()!=nonce:raise ValueError('remote-cycle identity differs')
        os.environ.update(MDDS_RECONNECT_CONTROL_ROOT=str(root),MDDS_RECONNECT_RUN=run,MDDS_RECONNECT_NONCE=nonce)
    command = [sys.executable, str(root / 'mdds_broker_service.py'), '--root', str(root / 'brokers'), '--domain', '175', '--daemon', str(root / 'mdds_broker_daemon'), '--token-exec', str(root / 'mdds_token_exec')]
elif role == 'ros':
    label = 'A' if self == '3e01ff55454d202020104033bf453b00' else 'B'
    command = [sys.executable, str(root / 'ros_broker_probe.py'), '--root', str(root), '--run-id', run, '--role', label, '--self-serial', self, '--peer-serial', peer, '--nonce', nonce, '--manifest-sha', os.environ['MDDS_RCLPY_MANIFEST_SHA']]
elif role == 'container':
    label = 'A' if self == '3e01ff55454d202020104033bf453b00' else 'B'
    command = [str(root/'component_prefix/lib/rclcpp_components/component_container'),'--ros-args','-r','__node:=container_'+label,'-r','__ns:=/components_'+run]
elif role == 'cli':
    module = 'cli_daemon.py' if (root/'cli_batch').read_text().strip() in ('daemon','action','introspection','parameter_read','parameter_write','lifecycle','components','standalone','bags','bag_transform','bag_burst','statistics','process_run','process_launch','process_test','diagnostics','hello','policy','multicast','trace_probe','service_qos','graph_waiters','graph_remote','graph_late','endpoint_qos','daemon_abort','graph_abrupt','graph_churn','graph_duplicate','graph_hidden') else 'cli_graph_basic.py'
    command = [sys.executable, str(root / module), str(root), run, self, peer, nonce]
else:
    raise ValueError('bad role')
returncode=supervise_command(command, root / (role + '.status.json'), run, role, '/ros_broker_' + run, root / (role + '.child.pid'))
if role=='ros' and (root/'graph_case').is_file():
    from cli_acceptance import terminal_marker
    case=(root/'graph_case').read_text().strip()
    if case not in ('graph:service_client_ownership','graph:endpoint_metadata','graph:duplicate_node_names','graph:churn','graph:abrupt_exit'):raise ValueError('unsupported graph case')
    print('MDDS_GRAPH_ACTUAL_ARGV '+json.dumps(command),flush=True)
    print(terminal_marker(run,case,returncode,command,self),flush=True)
raise SystemExit(returncode)
