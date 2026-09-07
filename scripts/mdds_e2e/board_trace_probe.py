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


def actor(root, run, role, phase, nonce):
    import rclpy
    from example_interfaces.srv import AddTwoInts
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
        value = {'node': name, 'pid': os.getpid(), 'start': process_start(os.getpid()),
                 'service': service, 'a': request.a, 'b': request.b, 'sum': future.result().sum,
                 'mount_namespace': os.readlink('/proc/self/ns/mnt')}
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
              'commands': [], 'actors': {}, 'passed': False}
    cli = [sys.executable, '-u', '-B', '-c', 'from ros2cli.cli import main; raise SystemExit(main())']

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
        report['commands'].append(value)
        print('TRACE_COMMAND ' + json.dumps(value), flush=True)
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
            report['commands'].append(value)
            print('TRACE_COMMAND ' + json.dumps(value), flush=True)
            if child.returncode != 0: raise RuntimeError('interactive trace failed')
            report['sessions'] = sessions
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
                if process_start(pid) == start: os.kill(pid, signal.SIGTERM)
            try: daemon.wait(timeout=3)
            except subprocess.TimeoutExpired: pass
            for pid, start in owned:
                if process_start(pid) == start:
                    try: os.kill(pid, signal.SIGKILL)
                    except ProcessLookupError: pass
            daemon.wait(timeout=5)
            report['namespace_cleanup'] = [{'pid': p, 'start': s} for p, s in owned]
            (folder / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    with tarfile.open(root / 'trace_probe.tar.gz', 'w:gz') as archive:
        archive.add(folder, arcname='trace_probe')
    print('TRACE_PROBE_ARCHIVE_SHA256=' + hashlib.sha256((root / 'trace_probe.tar.gz').read_bytes()).hexdigest(), flush=True)
    return report


def execute(root, run, role, nonce):
    actual = ['unshare', '-m', '--', sys.executable, '-I', '-B', str(root / 'trace_mount_namespace.py'),
              sys.executable, '-u', '-B', str(root / 'board_trace_probe.py'), 'worker', str(root), run, role, nonce]
    # Inherit the CLI process group so the outer owned-process supervisor can
    # terminate every child if HDC execution is interrupted.
    with subprocess.Popen(actual) as child:
        try: child.wait(timeout=180)
        except subprocess.TimeoutExpired: child.kill(); child.wait(); raise
    if child.returncode != 0: raise RuntimeError('private tracing probe failed')
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
