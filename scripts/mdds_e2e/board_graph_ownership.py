#!/usr/bin/env python3
"""Cross-board endpoint ownership and duplicate-node cardinality regression."""

import argparse
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time

LIBRARIES = ('libmdds.so', 'librmw_mdds.so')


def process_start(pid):
    try:
        # comm may contain spaces; parse only after its final closing bracket.
        return Path(f'/proc/{pid}/stat').read_text().rsplit(') ', 1)[1].split()[19]
    except (OSError, IndexError):
        return 'UNAVAILABLE'


def supervise_command(argv, status_path, run_id, role, namespace, child_record=None):
    """Wait for the actual Python process, including interpreter teardown."""
    status_path = Path(status_path)
    if status_path.exists() or status_path.is_symlink():
        raise FileExistsError(status_path)
    child = None
    old_handlers = {}

    def emergency_stop(signum, frame):
        if child is not None and child.poll() is None:
            try:
                if os.name == 'posix':
                    os.killpg(child.pid, signal.SIGKILL)
                else:
                    child.kill()
            except ProcessLookupError:
                pass

    try:
        for signum in (signal.SIGTERM, signal.SIGINT):
            old_handlers[signum] = signal.signal(signum, emergency_stop)
        child = subprocess.Popen(argv, start_new_session=(os.name == 'posix'))
        start = process_start(child.pid)
        if child_record is not None:
            if not start.isdecimal() or int(start) <= 0:
                raise RuntimeError('cannot bind the fixture child to its /proc start time')
            with Path(child_record).open('x', encoding='utf-8') as record:
                record.write(f'MDDS_OWNED_PROCESS RUN_ID={run_id} TAG={role}_child PID={child.pid} START={start}\n')
        returncode = child.wait()
        status = {'schema_version': 1, 'run_id': run_id, 'role': role,
                  'namespace': namespace, 'returncode': returncode,
                  'child_pid': child.pid, 'child_start': start}
        # The host polls this immutable record, then verifies its exact bytes.
        temporary = status_path.with_name(status_path.name + '.pending')
        with temporary.open('x', encoding='utf-8') as output:
            json.dump(status, output, sort_keys=True)
            output.write('\n')
            output.flush()
            os.fsync(output.fileno())
        if status_path.exists() or status_path.is_symlink():
            raise FileExistsError(status_path)
        print('GRAPH_PROCESS_EXIT ' + json.dumps(status, sort_keys=True), flush=True)
        temporary.rename(status_path)
        return returncode
    finally:
        if child is not None and child.poll() is None:
            emergency_stop(None, None)
            child.wait()
        for signum, handler in old_handlers.items():
            signal.signal(signum, handler)


def loaded_overlay(overlay):
    expected = {name: str(Path(overlay) / name) for name in LIBRARIES}
    found = {}
    for line in Path('/proc/self/maps').read_text().splitlines():
        fields = line.split()
        if fields and Path(fields[-1]).name in LIBRARIES:
            found.setdefault(Path(fields[-1]).name, set()).add(fields[-1])
    if found != {name: {path} for name, path in expected.items()}:
        raise RuntimeError(f'wrong loaded libraries: {found}; expected {expected}')
    print('GRAPH_LOADED_LIBS ' + json.dumps(expected, sort_keys=True), flush=True)


def validate_result(log, status, run_id, role, namespace, overlay):
    errors = []
    if not isinstance(status, dict):
        return ['missing or malformed process exit status']
    for key, expected in {'schema_version': 1, 'run_id': run_id, 'role': role,
                          'namespace': namespace}.items():
        if status.get(key) != expected:
            errors.append(f'wrong terminal {key}')
    if type(status.get('returncode')) is not int or status['returncode'] != 0:
        errors.append('the real fixture process did not exit successfully')
    if type(status.get('child_pid')) is not int or status['child_pid'] <= 0:
        errors.append('terminal child PID is invalid')
    start = status.get('child_start')
    if not isinstance(start, str) or not start.isdecimal() or int(start) <= 0:
        errors.append('terminal child start identity is invalid')
    lines = log.splitlines()
    results = [line for line in lines if line.startswith('GRAPH_OWNERSHIP_RESULT ')]
    if results != ['GRAPH_OWNERSHIP_RESULT PASS']:
        errors.append('missing, duplicate or failed ownership assertion')
    if lines.count('GRAPH_RMW=rmw_mdds') != 1:
        errors.append('the fixture RMW identity is missing or ambiguous')
    if role == 'source' and lines.count('GRAPH_SOURCE_READY') != 1:
        errors.append('source readiness is missing or ambiguous')
    transports = [line.split('mdds transports active: ', 1)[1]
                  for line in lines if 'mdds transports active: ' in line]
    if len(transports) != 1 or re.fullmatch(r'dsoftbus\([^)]*\)', transports[0]) is None:
        errors.append('the fixture did not select exactly one DSoftBus backend')
    maps = [line[len('GRAPH_LOADED_LIBS '):] for line in lines if line.startswith('GRAPH_LOADED_LIBS ')]
    expected_maps = {name: str(Path(overlay) / name).replace('\\', '/') for name in LIBRARIES}
    try:
        if len(maps) != 1 or json.loads(maps[0]) != expected_maps:
            errors.append('loaded libraries are not the exact run-owned overlay')
    except ValueError:
        errors.append('malformed loaded-library evidence')
    return errors


def inspect_node_cardinality(observer, namespace):
    count = sum(name == 'duplicate' and ns == namespace
                for name, ns in observer.get_node_names_and_namespaces())
    return [] if count == 2 else [{'node': 'duplicate', 'namespace': namespace,
                                  'expected_count': 2, 'actual_count': count}]


def inspect(observer, namespace):
    errors = inspect_node_cardinality(observer, namespace)
    for name in ('alpha', 'beta'):
        base = f'{namespace}/{name}'
        expected = {
            'publishers': (observer.get_publisher_names_and_types_by_node,
                           base + '/out', 'std_msgs/msg/String'),
            'subscriptions': (observer.get_subscriber_names_and_types_by_node,
                              base + '/in', 'std_msgs/msg/String'),
            'services': (observer.get_service_names_and_types_by_node,
                         base + '/serve', 'example_interfaces/srv/AddTwoInts'),
            'clients': (observer.get_client_names_and_types_by_node,
                        base + '/request', 'example_interfaces/srv/AddTwoInts'),
        }
        for kind, (getter, topic, type_name) in expected.items():
            actual = dict(getter(name, namespace))
            # Select the endpoints created by this ownership regression. ROS
            # may also create parameter/type-description services; topic vs
            # service demangling is a separate graph contract test.
            suffix = topic.rsplit('/', 1)[1]
            candidates = {f'{namespace}/{peer}/{suffix}' for peer in ('alpha', 'beta')}
            owned = {key: value for key, value in actual.items()
                     if key in candidates}
            if owned != {topic: [type_name]}:
                errors.append({'node': name, 'kind': kind, 'expected': topic,
                               'actual': owned})
        for getter, suffix in ((observer.get_publishers_info_by_topic, '/out'),
                               (observer.get_subscriptions_info_by_topic, '/in')):
            endpoints = getter(base + suffix)
            owners = [(ep.node_namespace, ep.node_name) for ep in endpoints]
            if owners != [(namespace, name)]:
                errors.append({'endpoint': base + suffix, 'owners': owners})
    return errors


def run_fixture(args):
    import rclpy
    from example_interfaces.srv import AddTwoInts
    from rclpy.signals import SignalHandlerOptions
    from rclpy.utilities import get_rmw_implementation_identifier
    from std_msgs.msg import String

    rclpy.init(signal_handler_options=SignalHandlerOptions.NO)
    nodes = []
    try:
        loaded_overlay(args.overlay)
        return run_graph(args, rclpy, nodes, AddTwoInts, String, get_rmw_implementation_identifier)
    finally:
        for node in reversed(nodes):
            node.destroy_node()
        rclpy.shutdown()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--role', choices=('source', 'observer', 'supervisor', 'verify'), required=True)
    parser.add_argument('--child-role', choices=('source', 'observer'))
    parser.add_argument('--namespace', required=True)
    parser.add_argument('--seconds', type=float, default=35)
    parser.add_argument('--overlay', required=True)
    parser.add_argument('--run-id', required=True)
    parser.add_argument('--stop-file')
    parser.add_argument('--child-record')
    parser.add_argument('--status-file')
    parser.add_argument('--log')
    args = parser.parse_args()
    if args.role == 'verify':
        status = json.loads(Path(args.status_file).read_text())
        errors = validate_result(Path(args.log).read_text(), status, args.run_id,
                                 args.child_role, args.namespace, args.overlay)
        record = Path(args.child_record).read_text().strip()
        expected = (f'MDDS_OWNED_PROCESS RUN_ID={args.run_id} TAG={args.child_role}_child '
                    f'PID={status.get("child_pid")} START={status.get("child_start")}')
        if record != expected:
            errors.append('terminal identity differs from the registered child record')
        print(json.dumps({'role': args.child_role, 'errors': errors}, sort_keys=True))
        return 1 if errors else 0
    if args.role == 'supervisor':
        command = [sys.executable, str(Path(__file__).resolve()), '--role', args.child_role,
                   '--namespace', args.namespace, '--seconds', str(args.seconds),
                   '--overlay', args.overlay, '--run-id', args.run_id]
        if args.stop_file:
            command += ['--stop-file', args.stop_file]
        return supervise_command(command, args.status_file, args.run_id, args.child_role,
                                  args.namespace, args.child_record)
    result = run_fixture(args)
    print('GRAPH_OWNERSHIP_RESULT ' + ('PASS' if result == 0 else 'FAIL'), flush=True)
    return result


def run_graph(args, rclpy, nodes, AddTwoInts, String, get_rmw_implementation_identifier):
    identifier = get_rmw_implementation_identifier()
    if identifier != 'rmw_mdds':
        raise RuntimeError(f'wrong RMW: {identifier}')
    print('GRAPH_RMW=' + identifier, flush=True)
    deadline = time.monotonic() + args.seconds
    if args.role == 'source':
        for name in ('alpha', 'beta'):
            node = rclpy.create_node(name, namespace=args.namespace,
                                     start_parameter_services=False,
                                     enable_rosout=False)
            nodes.append(node)
            base = f'{args.namespace}/{name}'
            node.create_publisher(String, base + '/out', 10)
            node.create_subscription(String, base + '/in', lambda msg: None, 10)
            node.create_service(AddTwoInts, base + '/serve', lambda req, res: res)
            node.create_client(AddTwoInts, base + '/request')
        for _ in range(2):
            nodes.append(rclpy.create_node('duplicate', namespace=args.namespace,
                                          start_parameter_services=False,
                                          enable_rosout=False))
        print('GRAPH_SOURCE_READY', flush=True)
        while time.monotonic() < deadline:
            if args.stop_file and Path(args.stop_file).is_file():
                if Path(args.stop_file).is_symlink() or Path(args.stop_file).read_text().strip() != f'GRAPH_STOP RUN_ID={args.run_id}':
                    raise RuntimeError('source stop record has the wrong run identity')
                return 0
            time.sleep(0.1)
        raise RuntimeError('source did not receive its run-owned stop request')
    observer = rclpy.create_node('observer', namespace=args.namespace,
                                 start_parameter_services=False,
                                 enable_rosout=False)
    nodes.append(observer)
    expected_nodes = {('alpha', args.namespace), ('beta', args.namespace)}
    errors = [{'error': 'remote graph did not converge'}]
    while time.monotonic() < deadline:
        if expected_nodes.issubset(set(observer.get_node_names_and_namespaces())):
            errors = inspect(observer, args.namespace)
            if not errors:
                return 0
        time.sleep(0.2)
    print('GRAPH_OWNERSHIP_ERRORS ' + json.dumps(errors, sort_keys=True), flush=True)
    return 1
if __name__ == '__main__':
    raise SystemExit(main())
