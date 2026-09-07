"""Owned private-namespace LTTng probe; acceptance requires host CTF decoding."""
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tarfile
import time
from board_graph_ownership import process_start, loaded_overlay
from trace_runtime import verify as verify_runtime, mapped
from trace_contract import recipe
from board_trace_receiver import payload
import cli_acceptance as acceptance


def actor(root, run, role, phase, nonce):
    import rclpy
    from example_interfaces.srv import AddTwoInts
    from std_msgs.msg import String
    from rclpy.qos import QoSProfile, ReliabilityPolicy
    name = 'trace_' + phase + '_' + role
    rclpy.init(args=[])
    node = rclpy.create_node(name)
    try:
        loaded_overlay(root / 'lib')
        peer = 'B' if role == 'A' else 'A'
        service = '/ros_broker_' + run + '/' + peer + '/alpha/serve'
        client = node.create_client(AddTwoInts, service)
        if not client.wait_for_service(timeout_sec=10):
            raise RuntimeError('trace actor cannot discover cross-board service')
        request = AddTwoInts.Request()
        request.a = int(nonce[:7], 16)
        request.b = list(('active', 'paused', 'resumed', 'stopped', 'interactive')).index(phase) + 1
        future = client.call_async(request)
        rclpy.spin_until_future_complete(node, future, timeout_sec=10)
        if not future.done() or future.result().sum != request.a + request.b:
            raise RuntimeError('trace actor cross-board response differs')
        qos = QoSProfile(depth=16, reliability=ReliabilityPolicy.RELIABLE)
        text = payload(run, nonce, role, phase)
        acks = []
        def acknowledge(message):
            if message.data != text or acks: raise ValueError('trace peer acknowledgement differs')
            acks.append(message.data)
        subscription = node.create_subscription(String, '/trace_' + run + '/' + role + '/ack', acknowledge, qos)
        publisher = node.create_publisher(String, '/trace_' + run + '/' + role + '/out', qos)
        deadline = time.monotonic() + 10
        while publisher.get_subscription_count() != 1 or node.count_publishers('/trace_' + run + '/' + role + '/ack') != 1:
            if time.monotonic() >= deadline: raise RuntimeError('trace peer endpoints not ready')
            rclpy.spin_once(node, timeout_sec=.05)
        message = String(); message.data = text; publisher.publish(message)
        while not acks:
            if time.monotonic() >= deadline: raise RuntimeError('trace peer publication not acknowledged')
            rclpy.spin_once(node, timeout_sec=.05)
        value = {'node': name, 'pid': os.getpid(), 'start': process_start(os.getpid()),
                 'service': service, 'a': request.a, 'b': request.b, 'sum': future.result().sum,
                 'mount_namespace': os.readlink('/proc/self/ns/mnt'), 'payload': text, 'peer_ack': True,
                 'trace_mappings': mapped(os.getpid(), root)}
        print('TRACE_ACTOR ' + json.dumps(value), flush=True)
        return value
    finally:
        node.destroy_node()
        rclpy.shutdown()


def worker(root, run, role, nonce):
    namespace = os.readlink('/proc/self/ns/mnt')
    if namespace == os.readlink('/proc/1/ns/mnt'):
        raise ValueError('tracing worker must have private mounts')
    folder = root / 'trace_probe'
    folder.mkdir()
    os.environ['LTTNG_HOME'] = str(folder / 'home')
    Path(os.environ['LTTNG_HOME']).mkdir()
    prefix = Path(os.environ['ROS2_HOME'])
    report = {'run_id': run, 'nonce': nonce, 'role': role, 'namespace': namespace,
              'system_namespace': os.readlink('/proc/1/ns/mnt'),
              'runtime_before': verify_runtime(root), 'commands': [], 'actors': {}, 'passed': False}
    cli = [sys.executable, '-u', '-B', '-c', 'from ros2cli.cli import main; raise SystemExit(main())']

    def save_command(value):
        label = value['label']
        case, receipt_label, args, expected = next(row for row in recipe(run, nonce) if row[1] == 'trace_' + label)
        board = acceptance.TARGET['board_serials'][0 if role == 'A' else 1]
        argv = ['ros2'] + args
        stdout = (folder / (label + '.stdout')).read_text()
        stderr = (folder / (label + '.stderr')).read_text()
        raw = ('MDDS_CLI_ACTUAL_ARGV ' + json.dumps(value['argv']) + '\nMDDS_CLI_STDOUT_BEGIN\n' + stdout
               + '\nMDDS_CLI_STDOUT_END\nMDDS_CLI_STDERR_BEGIN\n' + stderr + '\nMDDS_CLI_STDERR_END\n'
               + acceptance.terminal_marker(run, case, value['returncode'], argv, board) + '\n')
        if value['returncode'] == 0: raw += 'MDDS_CLI_FUNCTIONAL CASE=' + case + ' RESULT=PASS\n'
        log = root / 'cli_daemon' / (receipt_label + '.log'); log.write_text(raw)
        execution = {'argv': argv, 'actual_argv': value['argv'], 'child_pid': value['pid'], 'child_start': value['start'],
                     'board_serial': board, 'returncode': value['returncode'], 'log': {'path': log.name, 'sha256': acceptance.digest(log.read_bytes())}}
        report.setdefault('results', []).append({'case_id': case, 'label': receipt_label, 'expected': expected,
                                                'passed': value['returncode'] == 0, 'execution': execution})
        report['commands'].append(value)
        print('TRACE_COMMAND ' + json.dumps(value), flush=True)

    def command(label, args):
        actual = cli + args
        with (folder / (label + '.stdout')).open('wb') as out, (folder / (label + '.stderr')).open('wb') as err:
            with subprocess.Popen(actual, stdout=out, stderr=err) as child:
                start = process_start(child.pid)
                try:
                    child.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    child.kill(); child.wait(); raise
        value = {'label': label, 'argv': actual, 'pid': child.pid, 'start': start, 'returncode': child.returncode}
        save_command(value)
        if child.returncode:
            raise RuntimeError('trace command failed: ' + label)

    def run_actor(phase):
        actual = [sys.executable, '-u', '-B', str(root / 'board_trace_probe.py'), 'actor', str(root), run, role, phase, nonce]
        with (folder / (phase + '.actor.log')).open('wb') as out:
            with subprocess.Popen(actual, stdout=out, stderr=subprocess.STDOUT) as child:
                start = process_start(child.pid)
                try: child.wait(timeout=25)
                except subprocess.TimeoutExpired: child.kill(); child.wait(); raise
        raw = (folder / (phase + '.actor.log')).read_text()
        rows = [json.loads(line.removeprefix('TRACE_ACTOR ')) for line in raw.splitlines() if line.startswith('TRACE_ACTOR ')]
        if child.returncode != 0 or len(rows) != 1 or rows[0]['pid'] != child.pid or rows[0]['start'] != start:
            raise RuntimeError('traced actor failed: ' + phase)
        report['actors'][phase] = rows[0]

    def wait_prompt(child, path, prompt):
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline and child.poll() is None:
            if prompt in path.read_text(): return
            time.sleep(.05)
        raise RuntimeError('interactive trace prompt missing: ' + prompt)

    with (folder / 'sessiond.log').open('wb') as daemon_log:
        daemon = subprocess.Popen([str(prefix / 'bin/lttng-sessiond'), '--no-kernel'], stdout=daemon_log, stderr=subprocess.STDOUT)
        report['sessiond'] = {'pid': daemon.pid, 'start': process_start(daemon.pid)}
        try:
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline and daemon.poll() is None:
                if Path('/var/run/lttng/client-lttng-sessiond').is_socket(): break
                time.sleep(.1)
            else: raise RuntimeError('private sessiond not ready')
            sessions = {key: key + '_' + nonce[:12] for key in ('lifecycle', 'interactive')}
            options = ['--path', str(folder / 'traces'), '--ust', 'ros2:rcl_node_init', '--context', 'vpid']
            command('start', ['trace', 'start', sessions['lifecycle']] + options)
            run_actor('active')
            command('pause', ['trace', 'pause', sessions['lifecycle']])
            run_actor('paused')
            command('resume', ['trace', 'resume', sessions['lifecycle']])
            run_actor('resumed')
            command('stop', ['trace', 'stop', sessions['lifecycle']])
            run_actor('stopped')
            path = folder / 'interactive.stdout'
            args = cli + ['trace', '--session-name', sessions['interactive']] + options
            with path.open('wb') as out, (folder / 'interactive.stderr').open('wb') as err:
                with subprocess.Popen(args, stdin=subprocess.PIPE, stdout=out, stderr=err) as child:
                    start = process_start(child.pid)
                    try:
                        wait_prompt(child, path, 'press enter to start...')
                        child.stdin.write(b'\n'); child.stdin.flush()
                        wait_prompt(child, path, 'press enter to stop...')
                        run_actor('interactive')
                        child.stdin.write(b'\n'); child.stdin.flush()
                        child.wait(timeout=20)
                    finally:
                        if child.poll() is None: child.kill(); child.wait()
            value = {'label': 'interactive', 'argv': args, 'pid': child.pid, 'start': start, 'returncode': child.returncode}
            save_command(value)
            if child.returncode != 0: raise RuntimeError('interactive trace failed')
            report['sessions'] = sessions
            report['runtime_after'] = verify_runtime(root)
            report['passed'] = True
        finally:
            # Scan only this newly created namespace. PID/start identity is rechecked
            # before each signal; system-namespace daemons are never candidates.
            owned = []
            for proc in Path('/proc').iterdir():
                if not proc.name.isdecimal() or int(proc.name) == os.getpid(): continue
                try:
                    if os.readlink(proc / 'ns/mnt') == namespace:
                        owned.append((int(proc.name), process_start(int(proc.name))))
                except FileNotFoundError: pass
            for pid, start in owned:
                if process_start(pid) == start:
                    try: os.kill(pid, signal.SIGTERM)
                    except ProcessLookupError: pass
            try: daemon.wait(timeout=3)
            except subprocess.TimeoutExpired: pass
            for pid, start in owned:
                if process_start(pid) == start:
                    try: os.kill(pid, signal.SIGKILL)
                    except ProcessLookupError: pass
            daemon.wait(timeout=5)
            report['namespace_cleanup'] = [{'pid': p, 'start': s} for p, s in owned]
            deadline = time.monotonic() + 5
            remaining = owned
            while remaining and time.monotonic() < deadline:
                remaining = [(p, s) for p, s in remaining if process_start(p) == s and
                             Path(f'/proc/{p}/stat').read_text().rsplit(')', 1)[1].split()[0] != 'Z']
                if remaining: time.sleep(.05)
            report['cleanup_remaining'] = [{'pid': p, 'start': s} for p, s in remaining]
            if remaining: report['passed'] = False
            (folder / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    with tarfile.open(root / 'trace_probe.tar.gz', 'w:gz') as archive:
        archive.add(folder, arcname='trace_probe')
    print('TRACE_PROBE_ARCHIVE_SHA256=' + hashlib.sha256((root / 'trace_probe.tar.gz').read_bytes()).hexdigest(), flush=True)
    return report


def execute(root, run, role, nonce):
    from owned_trace_namespace import run as run_namespace
    actual = ['unshare', '-m', '--', sys.executable, '-I', '-B', str(root / 'trace_mount_namespace.py'),
              sys.executable, '-u', '-B', str(root / 'board_trace_probe.py'), 'worker', str(root), run, role, nonce]
    proof = run_namespace(actual, root / 'trace_supervisor.json', timeout=180)
    print('TRACE_NAMESPACE_RESULT ' + json.dumps(proof), flush=True)
    return json.loads((root / 'trace_probe/report.json').read_text())


if __name__ == '__main__':
    operation, directory, run_id, board_role, *rest = sys.argv[1:]
    root_path = Path(directory)
    if root_path != Path('/data/local/tmp/ros2/.mdds-owned-runs') / run_id:
        raise ValueError('wrong tracing root')
    if (root_path / 'owner').read_text() != f'MDDS_RUN_OWNER RUN_ID={run_id} LABEL=ros_broker\n':
        raise ValueError('wrong tracing owner')
    if operation == 'actor': actor(root_path, run_id, board_role, *rest)
    elif operation == 'worker': worker(root_path, run_id, board_role, *rest)
    else: raise ValueError('unknown tracing operation')
