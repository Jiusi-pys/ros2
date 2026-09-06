#!/usr/bin/env python3
"""Exercise actual rmw_mdds contexts/processes through an isolated Unix broker.

This is a local-only integration gate, not DSoftBus Socket/Bytes evidence.
With no overlay, the contexts case is also a regression oracle for the old
single-DSoftBus-context failure. Positive overlay runs must pass --overlay-lib.
"""

import argparse
from collections import Counter
import json
import os
from pathlib import Path
import re
import time

import rclpy
from rclpy.context import Context
from rclpy.duration import Duration
from rclpy.executors import SingleThreadedExecutor
from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy, HistoryPolicy
from rclpy.signals import SignalHandlerOptions
from rclpy.utilities import get_rmw_implementation_identifier
from std_msgs.msg import String


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def provenance(overlay_lib):
    paths = set()
    rmw_paths = set()
    for line in Path('/proc/self/maps').read_text().splitlines():
        parts = line.split(None, 5)
        if len(parts) == 6 and 'libmdds.so' in parts[5]:
            paths.add(parts[5].strip())
        if len(parts) == 6 and 'librmw_mdds.so' in parts[5]:
            rmw_paths.add(parts[5].strip())
    if overlay_lib:
        require(paths == {overlay_lib}, f'Unexpected libmdds mapping: {sorted(paths)}')
        expected_rmw = str(Path(overlay_lib).with_name('librmw_mdds.so'))
        require(rmw_paths == {expected_rmw}, f'Unexpected RMW mapping: {sorted(rmw_paths)}')
    socket_inodes = set()
    for fd in Path('/proc/self/fd').iterdir():
        try:
            target = os.readlink(fd)
        except FileNotFoundError:
            continue
        match = re.fullmatch(r'socket:\[(\d+)\]', target)
        if match:
            socket_inodes.add(match.group(1))
    udp = []
    for table_name in ('udp', 'udp6'):
        table = Path('/proc/net') / table_name
        if not table.exists():
            continue
        for line in table.read_text().splitlines()[1:]:
            columns = line.split()
            require(len(columns) >= 10, f'Malformed {table_name} socket inventory')
            if columns[9] in socket_inodes:
                udp.append({'table': table_name, 'local': columns[1], 'inode': columns[9]})
    require(not udp, f'Owned UDP sockets are forbidden: {udp}')
    return {'pid': os.getpid(), 'libmdds_paths': sorted(paths),
            'librmw_mdds_paths': sorted(rmw_paths), 'owned_udp_sockets': udp}


def check_profile():
    require(os.environ.get('MDDS_DEPLOYMENT_PROFILE') == 'ohos_dsoftbus',
            'Explicit ohos_dsoftbus profile required')
    require(os.environ.get('MDDS_TRANSPORT') is None, 'Legacy transport override is forbidden')


def new_node(name, namespace, receive_topic, transmit_topic):
    context = Context()
    record = {'context': context, 'node': None, 'executor': None, 'received': [], 'name': name}
    try:
        rclpy.init(context=context, signal_handler_options=SignalHandlerOptions.NO)
        require(get_rmw_implementation_identifier() == 'rmw_mdds', 'Wrong RMW implementation')
        node = rclpy.create_node(name, namespace=namespace, context=context)
        record['node'] = node
        qos = QoSProfile(depth=32, history=HistoryPolicy.KEEP_LAST,
                         reliability=ReliabilityPolicy.RELIABLE,
                         durability=DurabilityPolicy.VOLATILE)
        record['publisher'] = node.create_publisher(String, transmit_topic, qos)
        record['subscription'] = node.create_subscription(
            String, receive_topic, lambda message: record['received'].append(message.data), qos)
        executor = SingleThreadedExecutor(context=context)
        executor.add_node(node)
        record['executor'] = executor
        return record
    except BaseException:
        close_node(record)
        raise


def close_node(record):
    node = record.get('node')
    executor = record.get('executor')
    if node is not None:
        if executor is not None:
            executor.remove_node(node)
        node.destroy_node()
        record['node'] = None
    if executor is not None:
        require(executor.shutdown(timeout_sec=2), 'Executor did not shut down')
        record['executor'] = None
    context = record['context']
    if context.ok():
        context.shutdown()


def pump(records):
    for record in records:
        if record.get('executor') is not None:
            record['executor'].spin_once(timeout_sec=0.005)


def wait(records, predicate, timeout, detail):
    end = time.monotonic() + timeout
    last_error = None
    while time.monotonic() < end:
        pump(records)
        try:
            if predicate():
                return
        except RuntimeError as error:
            last_error = str(error)
    observed = []
    for record in records:
        try:
            observed.append({'observer': record['name'],
                             'nodes': record['node'].get_node_names_and_namespaces(),
                             'received': record['received']})
        except Exception as error:
            observed.append({'observer': record['name'], 'error': str(error)})
    raise RuntimeError(f'{detail}; last_error={last_error}; observed={observed}')


def topic_owners(node, topic, publisher):
    infos = (node.get_publishers_info_by_topic(topic) if publisher
             else node.get_subscriptions_info_by_topic(topic))
    return sorted((item.node_name, item.node_namespace, item.topic_type) for item in infos)


def complete_graph(observer, namespace, names, topic_a, topic_b, publisher_a, publisher_b):
    node = observer['node']
    current = Counter(name for name, ns in node.get_node_names_and_namespaces() if ns == namespace)
    if current != Counter(names):
        return False
    expected_a = [(publisher_a, namespace, 'std_msgs/msg/String')]
    expected_b = [(publisher_b, namespace, 'std_msgs/msg/String')]
    if (topic_owners(node, topic_a, True) != expected_a or
            topic_owners(node, topic_a, False) != expected_b or
            topic_owners(node, topic_b, True) != expected_b or
            topic_owners(node, topic_b, False) != expected_a):
        return False
    endpoint_ids = []
    for topic in (topic_a, topic_b):
        for info in (node.get_publishers_info_by_topic(topic) +
                     node.get_subscriptions_info_by_topic(topic)):
            identity = tuple(info.endpoint_gid)
            if not identity or not any(identity):
                return False
            endpoint_ids.append(identity)
    if len(set(endpoint_ids)) != 4:
        return False
    for owner, pub_topic, sub_topic in ((publisher_a, topic_a, topic_b),
                                       (publisher_b, topic_b, topic_a)):
        pubs = dict(node.get_publisher_names_and_types_by_node(owner, namespace))
        subs = dict(node.get_subscriber_names_and_types_by_node(owner, namespace))
        if pubs.get(pub_topic) != ['std_msgs/msg/String'] or subs.get(sub_topic) != ['std_msgs/msg/String']:
            return False
    return (node.count_publishers(topic_a) == 1 and node.count_subscribers(topic_a) == 1 and
            node.count_publishers(topic_b) == 1 and node.count_subscribers(topic_b) == 1)


def publish(record, payload):
    message = String()
    message.data = payload
    record['publisher'].publish(message)


def ack(record, publisher='publisher'):
    require(record[publisher].wait_for_all_acked(Duration(seconds=5)),
            f"Acknowledgement fence timed out for {record['name']}")


def contexts(args, namespace, topic_a, topic_b):
    records = []
    try:
        alpha = new_node('alpha', namespace, topic_b, topic_a)
        records.append(alpha)
        beta = new_node('beta', namespace, topic_a, topic_b)
        records.append(beta)
        wait(records, lambda: all(complete_graph(r, namespace, ['alpha', 'beta'],
                                                topic_a, topic_b, 'alpha', 'beta') for r in records),
             args.timeout, 'Both context graphs must show exact node and endpoint ownership')
        original_provenance = provenance(args.overlay_lib)
        wanted_alpha = [f'{args.run_id}:beta:{n}' for n in range(args.count)]
        wanted_beta = [f'{args.run_id}:alpha:{n}' for n in range(args.count)]
        for n in range(args.count):
            publish(alpha, wanted_beta[n])
            publish(beta, wanted_alpha[n])
            pump(records)
        wait(records, lambda: alpha['received'] == wanted_alpha and beta['received'] == wanted_beta,
             args.timeout, 'Exact bidirectional payload sequence was not received')
        print('BROKER_LOCAL_DELIVERY ' + json.dumps(
            {'run_id': args.run_id, 'phase': 'initial_contexts', 'alpha': alpha['received'],
             'beta': beta['received'], 'final_gate': False}, sort_keys=True), flush=True)
        ack(alpha)
        ack(beta)
        close_node(alpha)
        records.remove(alpha)
        wait(records, lambda: Counter(name for name, ns in beta['node'].get_node_names_and_namespaces()
                                      if ns == namespace) == Counter(['beta']) and
             not topic_owners(beta['node'], topic_a, True) and
             not topic_owners(beta['node'], topic_b, False),
             args.timeout, 'Retiring alpha must remove only its graph state')

        # A self-delivery on beta alone would not prove its broker connection
        # survived. A fresh third context must still communicate with beta.
        gamma = new_node('gamma', namespace, topic_b, topic_a)
        records.append(gamma)
        wait(records, lambda: all(complete_graph(r, namespace, ['beta', 'gamma'],
                                                topic_a, topic_b, 'gamma', 'beta') for r in records),
             args.timeout, 'Beta and replacement gamma graphs did not converge')
        after_alpha = f'{args.run_id}:gamma:after_alpha_shutdown'
        after_beta = f'{args.run_id}:beta:after_alpha_shutdown'
        publish(gamma, after_alpha)
        publish(beta, after_beta)
        wait(records, lambda: beta['received'] == wanted_beta + [after_alpha] and
             gamma['received'] == [after_beta], args.timeout,
             'Surviving beta lost its external broker path or replayed old volatile history')
        print('BROKER_LOCAL_DELIVERY ' + json.dumps(
            {'run_id': args.run_id, 'phase': 'after_alpha_retirement',
             'beta': beta['received'], 'gamma': gamma['received'], 'final_gate': False},
            sort_keys=True), flush=True)
        ack(beta)
        ack(gamma)
        return {'mode': 'contexts', 'before': original_provenance,
                'after': provenance(args.overlay_lib), 'initial_samples_per_direction': args.count,
                'alpha_retired': True, 'beta_to_fresh_gamma': True,
                'beta_received': beta['received'], 'gamma_received': gamma['received']}
    finally:
        for record in reversed(records):
            close_node(record)


def worker(args, namespace, topic_a, topic_b):
    role = args.role
    peer = 'beta' if role == 'alpha' else 'alpha'
    record = new_node(role, namespace, topic_b if role == 'alpha' else topic_a,
                      topic_a if role == 'alpha' else topic_b)
    try:
        qos = QoSProfile(depth=32, history=HistoryPolicy.KEEP_LAST,
                         reliability=ReliabilityPolicy.RELIABLE,
                         durability=DurabilityPolicy.VOLATILE)
        completion_topic = namespace + '/completion'
        control_received = set()
        allowed_control = {f'{args.run_id}:alpha:DONE', f'{args.run_id}:beta:DONE',
                           f'{args.run_id}:beta:RELEASE_ALPHA'}
        def on_control(message):
            if message.data in allowed_control:
                control_received.add(message.data)
        record['control_publisher'] = record['node'].create_publisher(String, completion_topic, qos)
        record['control_subscription'] = record['node'].create_subscription(
            String, completion_topic, on_control, qos)
        wait([record], lambda: complete_graph(record, namespace, ['alpha', 'beta'],
                                              topic_a, topic_b, 'alpha', 'beta') and
             record['node'].count_publishers(completion_topic) == 2 and
             record['node'].count_subscribers(completion_topic) == 2,
             args.timeout, f'{role} process graph did not converge')
        loaded = provenance(args.overlay_lib)
        expected = [f'{args.run_id}:{peer}:{n}' for n in range(args.count)]
        print('BROKER_WORKER_READY ' + role, flush=True)
        for n in range(args.count):
            publish(record, f'{args.run_id}:{role}:{n}')
            pump([record])
        wait([record], lambda: record['received'] == expected,
             args.timeout, f'{role} process missed or duplicated a payload')
        print('BROKER_LOCAL_DELIVERY ' + json.dumps(
            {'run_id': args.run_id, 'phase': 'worker_payload', 'role': role,
             'received': record['received'], 'final_gate': False}, sort_keys=True), flush=True)
        ack(record)
        message = String()
        message.data = f'{args.run_id}:{role}:DONE'
        record['control_publisher'].publish(message)
        wait([record], lambda: f'{args.run_id}:{peer}:DONE' in control_received,
             args.timeout, f'{role} did not observe peer exact-delivery completion')
        if role == 'beta':
            message.data = f'{args.run_id}:beta:RELEASE_ALPHA'
            record['control_publisher'].publish(message)
            ack(record, 'control_publisher')
            wait([record], lambda: Counter(name for name, ns in record['node'].get_node_names_and_namespaces()
                                          if ns == namespace) == Counter(['beta']),
                 args.timeout, 'Beta did not observe independent alpha process retirement')
        else:
            wait([record], lambda: f'{args.run_id}:beta:RELEASE_ALPHA' in control_received,
                 args.timeout, 'Alpha cannot exit before beta confirms its completion barrier')
            ack(record, 'control_publisher')
        return {'mode': 'worker', 'role': role, 'provenance': loaded,
                'received': record['received'], 'peer_retired': role == 'beta',
                'completion_barrier': True,
                'release_phase': 'sent' if role == 'beta' else 'received'}
    finally:
        close_node(record)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mode', choices=('contexts', 'worker'), required=True)
    parser.add_argument('--role', choices=('alpha', 'beta'))
    parser.add_argument('--run-id', required=True)
    parser.add_argument('--overlay-lib', default='')
    parser.add_argument('--count', type=int, default=5)
    parser.add_argument('--timeout', type=float, default=20)
    args = parser.parse_args()
    require(re.fullmatch(r'[A-Za-z0-9_]{1,48}', args.run_id) is not None, 'Invalid run id')
    require(1 <= args.count <= 16 and 0 < args.timeout <= 60, 'Invalid test bounds')
    require(args.mode != 'worker' or args.role is not None, 'Worker role is required')
    namespace = '/mdds_broker_' + args.run_id
    topic_a, topic_b = namespace + '/a_to_b', namespace + '/b_to_a'
    try:
        check_profile()
        result = (contexts(args, namespace, topic_a, topic_b) if args.mode == 'contexts'
                  else worker(args, namespace, topic_a, topic_b))
        result.update({'run_id': args.run_id, 'verdict': 'PASS', 'physical_dsoftbus_proven': False})
        print('BROKER_LOCAL_ROS_RESULT ' + json.dumps(result, sort_keys=True), flush=True)
        return 0
    except BaseException as error:
        print('BROKER_LOCAL_ROS_RESULT ' + json.dumps(
            {'run_id': args.run_id, 'mode': args.mode, 'role': args.role,
             'verdict': 'FAIL', 'error': str(error), 'physical_dsoftbus_proven': False},
            sort_keys=True), flush=True)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
