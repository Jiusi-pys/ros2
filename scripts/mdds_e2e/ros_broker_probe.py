# This probe verifies a graph/data subset; full type-hash/enclave/CLI gates are separate.
import argparse
import json
import os
from pathlib import Path
import time
import rclpy
from rclpy.context import Context
from rclpy.duration import Duration
from rclpy.executors import SingleThreadedExecutor
from rclpy.qos import QoSProfile, ReliabilityPolicy, HistoryPolicy
from rclpy.signals import SignalHandlerOptions
from rclpy.utilities import get_rmw_implementation_identifier
from std_msgs.msg import String
from example_interfaces.srv import AddTwoInts
from broker_local_ros_probe import provenance
p = argparse.ArgumentParser()
for arg in ('root', 'run-id', 'role', 'self-serial', 'peer-serial', 'nonce', 'manifest-sha'):
    p.add_argument('--' + arg, required=True)
a = p.parse_args()
root = Path(a.root)
other = 'B' if a.role == 'A' else 'A'
ns = '/ros_broker_' + a.run_id
assert a.role in ('A', 'B') and a.self_serial != a.peer_serial
assert os.environ['MDDS_BROKER_ROOT'] == str(root / 'brokers')
assert os.environ['MDDS_DEPLOYMENT_PROFILE'] == 'ohos_dsoftbus' and 'MDDS_TRANSPORT' not in os.environ
records = []
dups = []
contexts = []
all_nodes = []
executors = []

def payload(sender, name, phase, index):
    return f'{a.run_id}|{a.nonce}|{sender}|{name}|{phase}|{index}'

def path(role, name):
    return f'{ns}/{role}/{name}'

def spin():
    for e in executors:
        e.spin_once(timeout_sec=0)
    if any((r['bad'] for r in records)):
        raise RuntimeError('unexpected ROS payload')
    time.sleep(0.002)

def wait(predicate, seconds=65):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if predicate():
            return
        if (root / 'ros.stop').exists():
            raise RuntimeError('peer failed or stop arrived before phase completion')
        spin()
    raise RuntimeError('ROS broker phase deadline expired')

def snapshot(stage):
    value = provenance(str(root / 'lib/libmdds.so'), str(root / 'python'), str(root / 'rclpy_package.json'), a.manifest_sha)
    value.update(stage=stage, role=a.role)
    print('ROS_BROKER_PROVENANCE ' + json.dumps(value, sort_keys=True), flush=True)

def graph_ok(churn=False):
    observer = records[0]['node']
    nodes = observer.get_node_names_and_namespaces()
    for role in ('A', 'B'):
        if nodes.count(('duplicate_' + role, ns)) != (1 if churn else 2):
            return False
        for name in ('alpha', 'beta'):
            node_name = name + '_' + role
            base = path(role, name)
            if churn and name == 'beta':
                if (node_name, ns) in nodes:
                    return False
                if observer.get_publishers_info_by_topic(base + '/out'):
                    return False
                if observer.get_subscriptions_info_by_topic(path('B' if role == 'A' else 'A', name) + '/out'):
                    return False
                if base + '/serve' in dict(observer.get_service_names_and_types()):
                    return False
                if ns + '/' + node_name + '/get_type_description' in dict(observer.get_service_names_and_types()):
                    return False
                continue
            if nodes.count((node_name, ns)) != 1:
                return False
            pub = observer.get_publishers_info_by_topic(base + '/out')
            if len(pub) != 1 or pub[0].node_name != node_name or pub[0].node_namespace != ns:
                return False
            if pub[0].topic_type != 'std_msgs/msg/String' or pub[0].qos_profile.reliability != ReliabilityPolicy.RELIABLE:
                return False
            pubs = dict(observer.get_publisher_names_and_types_by_node(node_name, ns))
            subs = dict(observer.get_subscriber_names_and_types_by_node(node_name, ns))
            services = dict(observer.get_service_names_and_types_by_node(node_name, ns))
            clients = dict(observer.get_client_names_and_types_by_node(node_name, ns))
            peer = path('B' if role == 'A' else 'A', name)
            if pubs.get(base + '/out') != ['std_msgs/msg/String'] or subs.get(peer + '/out') != ['std_msgs/msg/String']:
                return False
            if services.get(base + '/serve') != ['example_interfaces/srv/AddTwoInts'] or clients.get(peer + '/serve') != ['example_interfaces/srv/AddTwoInts']:
                return False
    return True

def exchange(active, phase, services=False):
    pending = {r['name']: 0 for r in active}
    next_send = time.monotonic()

    def tick():
        nonlocal next_send
        now = time.monotonic()
        if now >= next_send:
            next_send = now + 0.05
            for r in active:
                index = pending[r['name']]
                if index < 5 and r['pub'].get_subscription_count() == 1:
                    msg = String()
                    msg.data = payload(a.self_serial, r['name'], phase, index)
                    r['pub'].publish(msg)
                    pending[r['name']] += 1
                if services and r['future'] is None and r['client'].wait_for_service(timeout_sec=0):
                    request = AddTwoInts.Request()
                    request.a = 41 if r['name'] == 'alpha' else 73
                    request.b = 17
                    r['sum'] = request.a + request.b
                    r['future'] = r['client'].call_async(request)
        for r in active:
            expected = [payload(a.peer_serial, r['name'], phase, i) for i in range(5)]
            got = [v for v in r['received'] if v in expected]
            if len(got) > 5:
                raise RuntimeError('duplicate ROS delivery')
            if sorted(got) != sorted(expected) or pending[r['name']] != 5:
                return False
            if services and (r['future'] is None or not r['future'].done()):
                return False
            if services and r['future'].result().sum != r['sum']:
                raise RuntimeError('incorrect service result')
        return graph_ok(churn=phase == 2)
    wait(tick)
    for r in active:
        if not r['pub'].wait_for_all_acked(Duration(seconds=3)):
            raise RuntimeError('reliable ACK wait timed out')
    received = {r['name']: [v for v in r['received'] if v in {payload(a.peer_serial, r['name'], phase, i) for i in range(5)}] for r in active}
    result = {'role': a.role, 'phase': phase, 'messages': 5 * len(active), 'service_calls': len(active) if services else 0, 'graph': True, 'ack': True, 'nonce': a.nonce, 'received': received, 'service_results': {r['name']: r['future'].result().sum for r in active} if services else {}, 'nodes': [list(v) for v in records[0]['node'].get_node_names_and_namespaces() if v[1] == ns]}
    result['type_hashes'] = {path(role, name) + '/out': [str(getattr(ep, 'topic_type_hash', None)) for ep in records[0]['node'].get_publishers_info_by_topic(path(role, name) + '/out')] for role in ('A', 'B') for name in ('alpha', 'beta')}
    print('ROS_BROKER_PHASE ' + json.dumps(result), flush=True)
try:
    for name in ('alpha', 'beta'):
        context = Context()
        contexts.append(context)
        rclpy.init(context=context, signal_handler_options=SignalHandlerOptions.NO)
        assert get_rmw_implementation_identifier() == 'rmw_mdds'
        node = rclpy.create_node(name + '_' + a.role, namespace=ns, context=context, start_parameter_services=False, enable_rosout=False)
        all_nodes.append(node)
        executor = SingleThreadedExecutor(context=context)
        executors.append(executor)
        executor.add_node(node)
        r = {'name': name, 'node': node, 'executor': executor, 'received': [], 'bad': [], 'future': None}
        records.append(r)
        qos = QoSProfile(depth=32, history=HistoryPolicy.KEEP_LAST, reliability=ReliabilityPolicy.RELIABLE)
        r['pub'] = node.create_publisher(String, path(a.role, name) + '/out', qos)
        allowed = {payload(a.peer_serial, name, phase, i) for phase in (1, 2) for i in range(5)}

        def callback(msg, r=r, allowed=allowed):
            (r['received'] if msg.data in allowed else r['bad']).append(msg.data)
        r['sub'] = node.create_subscription(String, path(other, name) + '/out', callback, qos)

        def serve(request, response):
            response.sum = request.a + request.b
            return response
        r['service'] = node.create_service(AddTwoInts, path(a.role, name) + '/serve', serve)
        r['client'] = node.create_client(AddTwoInts, path(other, name) + '/serve')
    for _ in range(2):
        n = rclpy.create_node('duplicate_' + a.role, namespace=ns, context=contexts[0], start_parameter_services=False, enable_rosout=False)
        all_nodes.append(n)
        dups.append(n)
        executors[0].add_node(n)
    snapshot(1)
    exchange(records, 1, True)
    (root / 'phase1.done').write_text(a.nonce + '\n')
    wait(lambda: (root / 'phase2.go').is_file() and (root / 'phase2.go').read_text().strip() == a.nonce)
    beta = records[1]
    beta['executor'].remove_node(beta['node'])
    beta['node'].destroy_node()
    all_nodes.remove(beta['node'])
    duplicate = dups.pop()
    executors[0].remove_node(duplicate)
    duplicate.destroy_node()
    all_nodes.remove(duplicate)
    exchange(records[:1], 2)
    snapshot(2)
    assert graph_ok(True)
    (root / 'ros.done').write_text(a.nonce + '\n')
    deadline = time.monotonic() + 40
    withdrew = False
    while time.monotonic() < deadline:
        if (root / 'ros.stop').is_file() and (root / 'ros.stop').read_text().strip() == a.nonce:
            break
        if a.role == 'B' and (not withdrew) and (root / 'peer_exit.go').is_file():
            assert (root / 'peer_exit.go').read_text().strip() == a.nonce

            def peer_gone():
                n = records[0]['node']
                return not any((name in ('alpha_A', 'beta_A', 'duplicate_A') and space == ns for name, space in n.get_node_names_and_namespaces())) and (not n.get_publishers_info_by_topic(path('A', 'alpha') + '/out')) and (records[0]['pub'].get_subscription_count() == 0) and (not records[0]['client'].wait_for_service(timeout_sec=0))
            wait(peer_gone)
            print('ROS_BROKER_PEER_WITHDRAWN ' + json.dumps({'role': 'B', 'nonce': a.nonce, 'service_available': False, 'matched_subscriptions': 0, 'nodes': [list(v) for v in records[0]['node'].get_node_names_and_namespaces() if v[1] == ns]}), flush=True)
            (root / 'peer_exit.done').write_text(a.nonce + '\n')
            withdrew = True
        spin()
    else:
        raise RuntimeError('no final owned stop')
    if a.role == 'B':
        assert withdrew
    print('ROS_BROKER_RESULT PASS role=' + a.role, flush=True)
finally:
    for n in reversed(all_nodes):
        n.destroy_node()
    for e in executors:
        e.shutdown(timeout_sec=1)
    for c in contexts:
        rclpy.try_shutdown(context=c)
        c.destroy()
