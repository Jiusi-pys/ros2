"""Validate the complete narrow ROS broker receipt; not the full delivery gate."""
from pathlib import Path
from collections import Counter
import hashlib
import json
import sys

def require(test, message):
    if not test:
        raise ValueError(message)

def json_rows(log, prefix):
    return [json.loads(x[len(prefix):]) for x in log.splitlines() if x.startswith(prefix)]

def row(log, prefix):
    rows = [x[len(prefix):] for x in log.splitlines() if x.startswith(prefix)]
    require(len(rows) == 1, 'expected one ' + prefix)
    return dict((x.split('=', 1) for x in rows[0].split()))

def validate(root, run, a, b):
    root = Path(root)
    report = {'passed': False, 'scope': 'two-board ROS contexts, exact reliable pubsub/service, graph ownership/cardinality/node retirement', 'full_graph_gate': False, 'full_cli_gate': False, 'errors': [], 'boards': []}
    try:
        nonce = (root / 'nonce').read_text().strip()
        remote = '/data/local/tmp/ros2/.mdds-owned-runs/' + run
        ns = '/ros_broker_' + run
        for board, peer, label in ((a, b, 'A'), (b, a, 'B')):
            logs = {role: (root / f'{board}.{role}.log').read_text() for role in ('ros', 'daemon')}
            status = {role: json.loads((root / f'{board}.{role}.status.json').read_text()) for role in logs}
            for role, s in status.items():
                require(s['run_id'] == run and s['role'] == role and (s['namespace'] == ns), 'wrong actual child identity')
                require(s['returncode'] == 0, f'{board}/{role} child exit {s['returncode']}; inspect original log')
                require((root / f'{board}.{role}.child.pid').read_text().strip() == f'MDDS_OWNED_PROCESS RUN_ID={run} TAG={role}_child PID={s['child_pid']} START={s['child_start']}', 'wrong PID/start record')
                require(json_rows(logs[role], 'GRAPH_PROCESS_EXIT ') == [s], 'log/terminal mismatch')
            require(logs['ros'].splitlines().count('ROS_BROKER_RESULT PASS role=' + label) == 1, 'missing exact ROS success')
            withdrawal = json_rows(logs['ros'], 'ROS_BROKER_PEER_WITHDRAWN ')
            if label == 'B':
                require(len(withdrawal) == 1 and withdrawal[0]['nonce'] == nonce, 'missing peer-process withdrawal')
                require(not withdrawal[0]['service_available'] and withdrawal[0]['matched_subscriptions'] == 0, 'stale service/match after peer exit')
                require(Counter((tuple(v) for v in withdrawal[0]['nodes'])) == Counter({('alpha_B', ns): 1, ('duplicate_B', ns): 1}), 'stale node after peer process exit')
            else:
                require(not withdrawal, 'unexpected withdrawal reporter')
            phases = json_rows(logs['ros'], 'ROS_BROKER_PHASE ')
            require([v['phase'] for v in phases] == [1, 2], 'missing ROS phases')
            for phase in phases:
                n = phase['phase']
                names = ('alpha', 'beta') if n == 1 else ('alpha',)
                require(phase['role'] == label and phase['nonce'] == nonce and phase['ack'] and phase['graph'], 'wrong phase binding')
                require(phase['messages'] == 5 * len(names), 'wrong message count')
                expected = {name: sorted((f'{run}|{nonce}|{peer}|{name}|{n}|{i}' for i in range(5))) for name in names}
                require({k: sorted(v) for k, v in phase['received'].items()} == expected, 'wrong exact ROS payloads')
                require(phase['service_results'] == ({'alpha': 58, 'beta': 90} if n == 1 else {}), 'wrong service responses')
                expected_nodes = Counter({(name + '_' + role, ns): 1 for name in names for role in ('A', 'B')})
                for role in ('A', 'B'):
                    expected_nodes['duplicate_' + role, ns] = 2 if n == 1 else 1
                require(Counter((tuple(v) for v in phase['nodes'])) == expected_nodes, 'wrong complete node cardinality')
            provenance = json_rows(logs['ros'], 'ROS_BROKER_PROVENANCE ')
            require([v['stage'] for v in provenance] == [1, 2], 'missing provenance')
            for v in provenance:
                require(not v['owned_udp_sockets'] and v['pid'] == status['ros']['child_pid'], 'ROS process UDP/identity mismatch')
                require(v['libmdds_paths'] == [remote + '/lib/libmdds.so'] and v['librmw_mdds_paths'] == [remote + '/lib/librmw_mdds.so'], 'wrong loaded ROS libraries')
                require(v['rclpy']['native_sha256'] == json.loads((root / 'rclpy_package.json').read_text())['native_sha256'], 'wrong rclpy native')
            inspected = json.loads((root / f'{board}.daemon.inspect.json').read_text())
            require(not inspected['owned_udp'] and len(inspected['sdk']) == 1, 'native broker SDK/UDP mismatch')
            require(inspected['pid'] == status['daemon']['child_pid'] and inspected['start'] == status['daemon']['child_start'], 'wrong inspected daemon')
            require(inspected['binary_sha256'] == hashlib.sha256((root / 'mdds_broker_daemon').read_bytes()).hexdigest(), 'wrong daemon executable')
            stop = row(logs['daemon'], 'MDBC_REMOTE_STOP ')
            require(stop['result'] == 'PASS' and stop['run_id'] == 'd175', 'daemon did not stop cleanly')
            require(all((stop[k] == '0' for k in ('connections', 'active_ports', 'remote_links', 'channels', 'pending_retirements', 'queued_bytes', 'reassembly_bytes'))), 'daemon resources remain')
            report['boards'].append({'board': board, 'status': status, 'phases': phases, 'withdrawal': withdrawal, 'provenance': provenance, 'daemon': inspected, 'stop': stop})
        report['passed'] = True
    except Exception as error:
        report['errors'].append(str(error))
    return report
if __name__ == '__main__':
    if len(sys.argv) != 6 or sys.argv[3] != 'service':
        raise SystemExit('usage: verify_ros_broker.py LOGDIR RUN_ID service BOARD_A BOARD_B')
    root = Path(sys.argv[1])
    report = validate(root, sys.argv[2], sys.argv[4], sys.argv[5])
    (root / 'host_report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'passed': report['passed'], 'scope': report['scope'], 'full_graph_gate': False, 'full_cli_gate': False, 'errors': report['errors']}))
    raise SystemExit(0 if report['passed'] else 1)
