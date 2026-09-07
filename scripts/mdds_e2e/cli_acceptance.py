#!/usr/bin/env python3
"""Enumerate installed ROS 2 CLI features and enforce the MDDS phase gate.

This validates coverage and evidence integrity. Functional runners must still
implement the declared assertions; an entry point, --help, count-only action
probe, or a different RMW baseline cannot satisfy a functional case.
"""

import argparse
import configparser
import copy
import hashlib
import json
from pathlib import Path
import re
import sys


TARGET = {
    'rmw_implementation': 'rmw_mdds',
    'transport': 'dsoftbus',
    'transport_scope': 'middleware',
    'board_serials': ['3e01ff55454d202020104033bf453b00', '3e01ff55454d202020104433991c3b00'],
}
DEFAULT_OPERATIONS = {'doctor', 'wtf', 'trace'}
STATUSES = {'NOT_RUN', 'PASS', 'FAIL', 'BLOCKED', 'SKIP'}

# Each recipe is a minimum functional oracle, not a claim that it has run.
# The installed inventory determines coverage; a newly installed verb receives
# an explicit unimplemented recipe and remains NOT_RUN until reviewed.
RECIPES = {
    'action/info': 'Inspect the remote action server and client names and exact counts.',
    'action/list': 'Find the isolated remote action with its exact canonical type.',
    'action/type': 'Return action_tutorials_interfaces/action/Fibonacci for the remote action.',
    'action/send_goal': 'Send order 5 to the peer with --feedback; assert accepted goal, feedback, exact result sequence [0, 1, 1, 2, 3, 5] and SUCCEEDED status.',
    'node/list': 'List exactly the fixture nodes, including multiple nodes in one remote context; compare daemon and --no-daemon modes.',
    'node/info': 'Verify each remote fixture node owns exactly its own publishers, subscriptions, services, clients and actions.',
    'topic/list': 'Find all remote topics and canonical types; compare visible and --include-hidden-topics modes.',
    'topic/info': 'Use --verbose and verify endpoint node, namespace, GID, type hash and actual QoS on both boards.',
    'topic/type': 'Return the exact type of the remote isolated topic.',
    'topic/find': 'Find all and only fixture topics of the requested type.',
    'topic/pub': 'Publish a run-specific payload with --once; require the peer callback to receive exactly that payload.',
    'topic/echo': 'Receive a run-specific peer payload with --once, and validate field/filter and QoS options with matching fixtures.',
    'topic/hz': 'Collect multiple remote samples and assert the reported numeric rate within the fixture tolerance.',
    'topic/bw': 'Collect remote samples and compare reported bandwidth and message size against the fixture payload.',
    'topic/delay': 'Publish synchronized Header-bearing messages remotely and assert finite delay measurements within a declared bound.',
    'service/list': 'List all remote fixture services with exact types, including hidden filtering.',
    'service/type': 'Return example_interfaces/srv/AddTwoInts for the remote isolated service.',
    'service/find': 'Find all and only the fixture services of the requested type.',
    'service/info': 'Assert exact remote client/server counts and service type.',
    'service/call': 'Call the peer with run-specific operands and assert the exact sum and successful completion.',
    'service/echo': 'Enable service introspection on a fixture, execute a peer call and validate request/response event content and identity.',
    'param/list': 'List the peer fixture parameters and assert their exact names.',
    'param/get': 'Read each supported scalar/array type from the peer and assert exact values.',
    'param/set': 'Change the peer parameter, read it back, assert the matching parameter event, then restore the initial value.',
    'param/describe': 'Verify peer parameter type, description, read-only flag and constraints.',
    'param/delete': 'Delete a declared dynamic peer parameter; assert that it disappears and a subsequent get fails as expected.',
    'param/dump': 'Dump peer parameters to a run-owned YAML file and verify exact names, types and values.',
    'param/load': 'Load a run-owned YAML file into the peer and verify every changed value by get.',
    'lifecycle/nodes': 'Find the remote lifecycle fixture and exclude ordinary nodes.',
    'lifecycle/get': 'Read the remote fixture state and assert its label and ID.',
    'lifecycle/list': 'Verify the available transitions for the current remote fixture state.',
    'lifecycle/set': 'Drive configure, activate, deactivate, cleanup and shutdown remotely; verify state and transition events after each step.',
    'component/types': 'List the installed composition fixtures and exact plugin type names.',
    'component/list': 'Inspect the peer component container and verify its loaded node IDs/names.',
    'component/load': 'Load a Talker into the peer container; verify assigned ID, node ownership and received payload.',
    'component/unload': 'Unload the exact run-owned component ID and assert its graph endpoints disappear while other components remain.',
    'component/standalone': 'Start an installed component through standalone; receive its payload remotely and cleanly stop its owned process.',
    'daemon/start': 'Start a run-owned daemon using rmw_mdds and prove it observes remote fixture nodes.',
    'daemon/status': 'Assert running/stopped status around an owned daemon lifecycle.',
    'daemon/stop': 'Stop the owned daemon; verify process exit and successful subsequent no-daemon queries.',
    'extension_points': 'Execute the inventory command and verify ros2cli.command and every declared verb extension group.',
    'extensions': 'Execute the inventory command and compare registered commands/verbs to this manifest.',
    'interface/list': 'Verify installed message, service and action names appear in the correct sections.',
    'interface/package': 'Verify exact interfaces exported by a selected installed fixture package.',
    'interface/packages': 'Verify installed interface packages including std_msgs and action_tutorials_interfaces.',
    'interface/show': 'Show a message, service and action; assert exact field names/types and section boundaries.',
    'interface/proto': 'Generate a prototype, parse it as YAML and verify the selected interface field values.',
    'pkg/list': 'Find rmw_mdds and all packages required by the fixtures.',
    'pkg/prefix': 'Resolve rmw_mdds to the deployed prefix and verify the package marker exists.',
    'pkg/xml': 'Parse the emitted rmw_mdds package XML and verify its name and RMW group membership.',
    'pkg/executables': 'Resolve installed C++ and Python demo executables and verify the actual files exist.',
    'pkg/create': 'Create a package only in the run-owned scratch directory and validate its generated package.xml/build files.',
    'run': 'Run installed C++ and Python demos through ros2 run and receive their exact payloads on the peer.',
    'launch': 'Launch a run-owned multi-node fixture with arguments/remaps; assert all remote graph entities and payloads, then verify shutdown.',
    'test': 'Run an installed launch-testing fixture through ros2 test; require real assertions and a passing test report.',
    'plugin/list': 'List plugins for an installed base class and verify the expected implementation and library.',
    'doctor': 'Run checks and --report against live fixtures; verify the reported RMW is rmw_mdds and diagnose any failing checks.',
    'wtf': 'Run the doctor alias against live fixtures and verify matching diagnostic behavior and rmw_mdds identity.',
    'doctor/hello': 'Exchange doctor hello messages with the peer and assert the participant/message report.',
    'wtf/hello': 'Exercise the hello verb through the wtf alias with peer message evidence.',
    'bag/list': 'List actual available reader, writer, storage and serialization plugins; verify sqlite3 and mcap.',
    'bag/record': 'Record run-specific peer samples to sqlite3 and mcap; stop cleanly and verify exact sample identities/counts.',
    'bag/info': 'Inspect a recorded fixture bag and compare storage ID, topics, types, count and duration to the fixture.',
    'bag/play': 'Replay the fixture bag through rmw_mdds to the peer; verify exact ordered payloads and counts.',
    'bag/burst': 'Use a paused player and burst the requested number of messages; assert exactly that many peer callbacks.',
    'bag/convert': 'Convert a run-owned fixture bag with an explicit output configuration; verify selected topics/types and exact samples.',
    'bag/reindex': 'Reindex a run-owned bag copy with missing metadata and verify restored metadata and playable exact samples.',
    'trace': 'Exercise interactive trace start/stop with run-owned session state and decode actual ROS events.',
    'trace/start': 'Start a run-owned tracing session, publish across rmw_mdds and decode actual rcl/rmw events.',
    'trace/pause': 'Pause the owned session and verify fixture events in the paused interval are absent.',
    'trace/resume': 'Resume the owned session and verify fixture events appear again.',
    'trace/stop': 'Stop the owned session, verify clean termination and decode the final trace artifact.',
    'multicast/send': 'Test the standalone UDP multicast diagnostic in an isolated utility fixture; this is not an MDDS transport or fallback test.',
    'multicast/receive': 'Receive the exact standalone multicast diagnostic payload; keep its UDP traffic separate from MDDS transport evidence.',
    'security/create_keystore': 'Create a run-owned scratch keystore and validate its generated CA/key files.',
    'security/create_enclave': 'Create a run-owned enclave and validate identities, signed permissions and governance files.',
    'security/create_key': 'Exercise the create_key alias in scratch and verify the generated enclave artifacts.',
    'security/list_enclaves': 'List exactly the enclaves created in the run-owned keystore.',
    'security/list_keys': 'Exercise the list_keys alias and verify the same scratch enclave identities.',
    'security/create_permission': 'Generate signed permissions for a scratch enclave from a known policy and validate its contents/signature.',
    'security/generate_artifacts': 'Generate scratch keystore/enclave artifacts from a fixture policy and validate all expected files.',
    'security/generate_policy': 'Observe isolated live rmw_mdds fixture nodes and verify generated policy topic/service/action permissions.',
}

NONCLI = {
    'graph:remote_multi_node': ('One remote participant has at least two nodes with disjoint pub/sub/service/client/action endpoints; every by-node query and verbose endpoint report must preserve exact ownership.', ['node_ownership_exact', 'endpoint_metadata_exact']),
    'graph:duplicate_node_names': ('Create identical node name/namespace in distinct contexts/participants; retain the required multiplicity and correct endpoints through one participant removal.', ['multiplicity_exact', 'survivor_preserved']),
    'graph:hidden_entities': ('Test hidden nodes/topics/services/actions and no-demangle APIs without leaking hidden entities into default CLI views.', ['visible_set_exact', 'hidden_set_exact']),
    'graph:endpoint_metadata': ('Check remote endpoint type, type hash, GID, node name/namespace, QoS and direction; reject QoS-incompatible matches.', ['metadata_exact', 'qos_match_exact']),
    'graph:service_client_ownership': ('Verify get_service_names_and_types_by_node, get_client_names_and_types_by_node, counts and availability for multiple clients/servers.', ['ownership_exact', 'counts_exact', 'availability_exact']),
    'graph:local_guard': ('Create/destroy nodes and each endpoint kind inside one context; graph guard must wake an already-blocked waiter after every change.', ['create_wakes', 'destroy_wakes']),
    'graph:remote_guard': ('Peer node/endpoint additions and removals must wake graph waiters and converge to the expected set.', ['remote_create_wakes', 'remote_destroy_wakes']),
    'graph:late_join': ('Start the observer after the peer multi-node graph is established and obtain the complete exact graph before a bounded deadline.', ['snapshot_complete']),
    'graph:churn': ('Repeatedly create/destroy remote nodes and endpoints while querying; assert no stale attribution, ghosts, crashes or missed final removal.', ['snapshot_consistent', 'final_set_exact']),
    'graph:abrupt_exit': ('Kill only a run-owned fixture and verify lease-bounded graph removal and unaffected surviving participants.', ['expired_removed', 'survivor_preserved']),
    'graph:reconnect': ('Interrupt and restore the fixture transport, restart the peer and verify fresh identities plus complete graph/data recovery without ghosts.', ['recovered_graph_exact', 'recovered_payload_exact']),
    'graph:domain_isolation': ('Run matching names/types in different domains; require no foreign graph, matches or samples, then prove same-domain positive control.', ['foreign_graph_zero', 'foreign_matches_zero', 'foreign_samples_zero', 'positive_control']),
    'graph:discovery_off': ('With discovery OFF require zero remote graph/matches/data; explicitly enabled control must communicate.', ['remote_graph_zero', 'remote_matches_zero', 'remote_samples_zero', 'positive_control']),
    'transport:a_to_b': ('Board A ROS 2 rmw_mdds payload must reach the exact board B callback through DSoftBus Socket/Listen/Bind/SendBytes/OnBytes.', ['socket_listen', 'socket_bind', 'send_bytes', 'on_bytes_exact', 'ros_payload_exact']),
    'transport:b_to_a': ('Reverse the real ROS 2 payload path and prove board B to A DSoftBus Socket API callbacks and exact delivery.', ['socket_listen', 'socket_bind', 'send_bytes', 'on_bytes_exact', 'ros_payload_exact']),
    'transport:no_udp_fallback': ('Set MDDS_TRANSPORT=dsoftbus and force DSoftBus startup/link failure; require failure or disconnection with no MDDS UDP socket/backend traffic, then restore and prove positive control.', ['dsoftbus_selected', 'failure_observed', 'udp_backend_absent', 'positive_control']),
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def discover_inventory(prefix):
    """Read metadata as data, without importing target-architecture extensions."""
    prefix = Path(prefix)
    roots = [prefix / 'Lib/site-packages']
    roots.extend(sorted((prefix / 'lib').glob('python*/site-packages')))
    files = sorted({path for root in roots for suffix in ('*.egg-info', '*.dist-info')
                    for path in root.glob(suffix + '/entry_points.txt')})
    commands, groups, records = {}, {}, []
    for path in files:
        raw = path.read_bytes()
        config = configparser.ConfigParser(interpolation=None, strict=True)
        config.optionxform = str
        config.read_string(raw.decode('utf-8'))
        relevant = [section for section in config.sections()
                    if section == 'ros2cli.command' or section.endswith('.verb')]
        if not relevant:
            continue
        records.append({'path': path.relative_to(prefix).as_posix(), 'sha256': digest(raw)})
        for section in relevant:
            output = commands if section == 'ros2cli.command' else groups.setdefault(section, {})
            for name, entry in config.items(section):
                if not re.fullmatch(r'[a-z][a-z0-9_]*', name) or not entry.strip():
                    raise ValueError(f'invalid CLI entry point: {section}/{name}')
                if name in output:
                    raise ValueError(f'duplicate CLI entry point: {section}/{name}')
                output[name] = entry.strip()
    if not commands:
        raise ValueError(f'no installed ros2cli.command entry points under {prefix}')
    return {'commands': dict(sorted(commands.items())),
            'verb_groups': {key: dict(sorted(value.items())) for key, value in sorted(groups.items())},
            'metadata_files': records}


def case_spec(case_id, command, requirement, assertions):
    cross_board = (
        case_id.startswith('transport:') or
        (case_id.startswith('graph:') and case_id != 'graph:local_guard') or
        (command and command[1] in {'action', 'node', 'topic', 'service', 'param', 'lifecycle', 'run', 'launch'}) or
        case_id in {'cli:component/list', 'cli:component/load', 'cli:component/unload',
                    'cli:component/standalone', 'cli:doctor/hello', 'cli:wtf/hello',
                    'cli:bag/record', 'cli:bag/play', 'cli:bag/burst', 'cli:daemon/start'})
    execution_boards = TARGET['board_serials'] if cross_board else TARGET['board_serials'][:1]
    return {'id': case_id, 'command': command, 'requirement': requirement,
            'assertions': assertions, 'execution_boards': list(execution_boards),
            'status': 'NOT_RUN', 'evidence': []}


def make_manifest(inventory):
    cases = []
    used_groups = set()
    for command, entry in inventory['commands'].items():
        package = entry.split('.', 1)[0]
        group = 'ros2cli.daemon.verb' if command == 'daemon' else package + '.verb'
        verbs = inventory['verb_groups'].get(group, {})
        if verbs:
            used_groups.add(group)
        paths = [[command, verb] for verb in verbs]
        if not verbs or command in DEFAULT_OPERATIONS:
            paths.insert(0, [command])
        for parts in paths:
            key = '/'.join(parts)
            assertions = ['functional_result']
            if key == 'action/send_goal':
                assertions = ['goal_accepted', 'feedback_received', 'result_exact', 'status_succeeded']
            cases.append(case_spec('cli:' + key, ['ros2'] + parts,
                                   RECIPES.get(key, 'UNIMPLEMENTED: define and review a real functional fixture for this installed command.'),
                                   assertions))
    orphaned = set(inventory['verb_groups']) - used_groups
    if orphaned:
        raise ValueError('unmapped installed verb groups: ' + ', '.join(sorted(orphaned)))
    for case_id, (requirement, assertions) in NONCLI.items():
        cases.append(case_spec(case_id, [], requirement, assertions))
    return {'schema_version': 1, 'run_id': None, 'target': copy.deepcopy(TARGET),
            'inventory': inventory, 'cases': sorted(cases, key=lambda case: case['id'])}


def terminal_marker(run_id, case_id, returncode, argv, board_serial):
    body = {'run_id': run_id, 'case_id': case_id, 'returncode': returncode,
            'board_serial': board_serial,
            'argv_sha256': digest(json.dumps(argv, ensure_ascii=True, separators=(',', ':')).encode('utf-8'))}
    return 'MDDS_CLI_TERMINAL ' + json.dumps(body, sort_keys=True, separators=(',', ':'))


def controlled_stop_marker(run_id, case_id, execution):
    body={'run_id':run_id,'case_id':case_id,'board_serial':execution['board_serial'],
          'argv_sha256':digest(json.dumps(execution['argv'],ensure_ascii=True,separators=(',',':')).encode('utf-8')),
          'stop':execution['controlled_stop']}
    return 'MDDS_CLI_CONTROLLED_STOP '+json.dumps(body,sort_keys=True,separators=(',',':'))


def validate_parameter_absence(execution, earlier, log):
    argv=execution.get('argv',[])
    if execution.get('expected_failure')!='parameter_not_set' or execution.get('returncode')!=1 or len(argv)!=5 or argv[:3]!=['ros2','param','get']:
        raise ValueError('not a parameter-absence query')
    if not any(item.get('board_serial')==execution['board_serial'] and item.get('returncode')==0 and item.get('argv')==['ros2','param','delete']+argv[3:] for item in earlier):
        raise ValueError('no earlier successful deletion of this parameter on this board')
    try:
        stdout=log.split('MDDS_CLI_STDOUT_BEGIN\n',1)[1].split('\nMDDS_CLI_STDOUT_END',1)[0]
        stderr=log.split('MDDS_CLI_STDERR_BEGIN\n',1)[1].split('\nMDDS_CLI_STDERR_END',1)[0]
    except IndexError:raise ValueError('missing parameter-absence output')
    errors=[line.strip() for line in stderr.splitlines() if line.strip() and not line.startswith('[INFO] [rmw_mdds]: mdds transports active: dsoftbus(')]
    if stdout.strip() or errors!=['Parameter not set']:raise ValueError('unexpected parameter-absence output')


def read_artifact(reference, root):
    if not isinstance(reference, dict) or set(reference) != {'path', 'sha256'}:
        raise ValueError('evidence reference must have exactly path and sha256')
    relative = reference['path']
    if not isinstance(relative, str) or not relative or '\\' in relative:
        raise ValueError('evidence path must be a nonempty relative POSIX path')
    path = Path(relative)
    if path.is_absolute() or '..' in path.parts or ':' in relative:
        raise ValueError('evidence path escapes the run evidence directory')
    root = Path(root).resolve()
    candidate = root / path
    if candidate.is_symlink() or not candidate.is_file() or not candidate.resolve().is_relative_to(root):
        raise ValueError(f'missing, symlinked or escaping evidence file: {relative}')
    expected = reference['sha256']
    if not isinstance(expected, str) or not re.fullmatch(r'[0-9a-f]{64}', expected):
        raise ValueError(f'invalid evidence SHA256: {relative}')
    data = candidate.read_bytes()
    if not data or digest(data) != expected:
        raise ValueError(f'empty or changed evidence file: {relative}')
    return data


def validate_receipt(case, reference, manifest, root):
    receipt = json.loads(read_artifact(reference, root))
    expected = {'schema_version': 1, 'run_id': manifest['run_id'], 'case_id': case['id'],
                'kind': 'functional', 'status': 'PASS', 'board_serials': TARGET['board_serials'],
                'rmw_implementation': TARGET['rmw_implementation'], 'transport': TARGET['transport']}
    if not isinstance(receipt, dict):
        raise ValueError('receipt is not an object')
    for key, value in expected.items():
        if receipt.get(key) != value:
            raise ValueError(f'receipt {key} does not match this run/target/functional case')
    executions = receipt.get('executions')
    if not isinstance(executions, list) or not executions:
        raise ValueError('functional receipt has no command executions')
    logs, matched_command, execution_boards = [], False, set()
    for execution_index,execution in enumerate(executions):
        if not isinstance(execution, dict):
            raise ValueError('execution must be an object')
        board_serial = execution.get('board_serial')
        if board_serial not in TARGET['board_serials']:
            raise ValueError('execution is not bound to a target board')
        execution_boards.add(board_serial)
        argv = execution['argv']
        if not isinstance(argv, list) or not argv or not all(isinstance(arg, str) and arg for arg in argv):
            raise ValueError('execution argv must be a nonempty string array')
        if '--help' in argv or '-h' in argv:
            raise ValueError('help output is not functional evidence')
        code=execution.get('returncode')
        controlled=case['id'] in ('cli:service/echo','cli:topic/hz','cli:topic/bw','cli:topic/delay','cli:doctor/hello','cli:wtf/hello')
        absent=case['id']=='cli:param/delete' and execution.get('expected_failure')=='parameter_not_set'
        if 'expected_failure' in execution and not absent:raise ValueError('unsupported expected failure')
        if type(code) is not int or code not in ((1,) if absent else ((0,2) if controlled else (0,))):
            raise ValueError('functional command did not terminate successfully')
        if not controlled and 'controlled_stop' in execution:
            raise ValueError('controlled stop is not supported for this case')
        required = case['command']
        if not required or argv[:len(required)] == required:
            matched_command = True
        log = read_artifact(execution['log'], root).decode('utf-8')
        if absent:validate_parameter_absence(execution,executions[:execution_index],log)
        if controlled:
            start=execution.get('child_start');pid=execution.get('child_pid')
            if type(pid) is not int or pid<=0 or not isinstance(start,str) or not start.isdecimal():
                raise ValueError('controlled stop lacks child identity')
            begin='MDDS_CLI_STDOUT_BEGIN\n';end='\nMDDS_CLI_STDOUT_END\n'
            if log.count(begin)!=1 or log.count(end)!=1:raise ValueError('controlled stop lacks captured stdout')
            stdout=log.split(begin,1)[1].split(end,1)[0]
            expected_stop={'signal':2,'reason':'functional_observed','child_pid':pid,'child_start':start,'stdout_sha256':digest(stdout.encode('utf-8'))}
            if execution.get('controlled_stop')!=expected_stop:raise ValueError('controlled stop identity or observed stdout differs')
            stop_marker=controlled_stop_marker(manifest['run_id'],case['id'],execution)
            if log.splitlines().count(stop_marker)!=1:raise ValueError('controlled stop marker missing or repeated')
        marker = terminal_marker(manifest['run_id'], case['id'], code, argv, board_serial)
        if log.splitlines().count(marker) != 1:
            raise ValueError('raw log lacks exactly one matching command terminal marker')
        logs.append(log)
    if not matched_command:
        raise ValueError('the declared CLI command was not executed; a probe is not a substitute')
    if not set(case['execution_boards']) <= execution_boards:
        raise ValueError('functional evidence is missing execution logs from a required board')
    assertions = receipt.get('assertions')
    if not isinstance(assertions, list):
        raise ValueError('receipt assertions must be an array')
    if not all(isinstance(assertion, dict) and isinstance(assertion.get('id'), str)
               for assertion in assertions):
        raise ValueError('each assertion must have a string ID')
    ids = [assertion['id'] for assertion in assertions]
    if len(ids) != len(set(ids)) or set(ids) != set(case['assertions']):
        raise ValueError('receipt assertion coverage differs from the required case')
    for assertion in assertions:
        if assertion.get('passed') is not True:
            raise ValueError(f'functional assertion did not pass: {assertion["id"]}')
        index, pattern = assertion.get('execution'), assertion.get('pattern')
        if type(index) is not int or not 0 <= index < len(logs):
            raise ValueError('assertion has no matching raw execution log')
        if not isinstance(pattern, str) or not pattern.strip() or pattern not in logs[index]:
            raise ValueError('functional assertion lacks its exact observed text in the raw log')
        if pattern.startswith('MDDS_CLI_TERMINAL'):
            raise ValueError('a command exit marker is not a functional assertion')


def validate_manifest(manifest, inventory, evidence_root, phase=1):
    errors, passed, total = [], 0, 0
    try:
        if phase not in (1, 2):
            raise ValueError('phase must be 1 or 2')
        if not isinstance(manifest, dict) or manifest.get('schema_version') != 1:
            raise ValueError('unsupported manifest schema')
        if manifest.get('target') != TARGET:
            raise ValueError('target must be the pinned two-board rmw_mdds/DSoftBus target')
        run_id = manifest.get('run_id')
        if not isinstance(run_id, str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]*', run_id):
            errors.append('a run_id is required; the inventory template is not execution evidence')
        if manifest.get('inventory') != inventory:
            errors.append('installed entry-point inventory differs from the manifest snapshot')
        expected = {case['id']: case for case in make_manifest(inventory)['cases']}
        total = len(expected)
        cases = manifest.get('cases')
        if not isinstance(cases, list) or not cases:
            raise ValueError('case inventory is empty or malformed')
        seen = set()
        for case in cases:
            if not isinstance(case, dict) or not isinstance(case.get('id'), str):
                errors.append('malformed case object')
                continue
            case_id = case['id']
            if case_id in seen:
                errors.append(f'{case_id}: duplicate case')
                continue
            seen.add(case_id)
            spec = expected.get(case_id)
            if spec is None:
                errors.append(f'{case_id}: unknown case')
                continue
            if any(case.get(key) != spec[key] for key in ('command', 'requirement', 'assertions', 'execution_boards')):
                errors.append(f'{case_id}: functional acceptance criteria changed')
                continue
            if spec['requirement'].startswith('UNIMPLEMENTED:'):
                errors.append(f'{case_id}: installed command has no reviewed functional fixture')
                continue
            status = case.get('status')
            if not isinstance(status, str) or status not in STATUSES or status != 'PASS':
                errors.append(f'{case_id}: status {status!r} is not PASS')
                continue
            evidence = case.get('evidence')
            if not isinstance(evidence, list) or len(evidence) != 1:
                errors.append(f'{case_id}: exactly one complete functional receipt is required')
                continue
            try:
                validate_receipt(case, evidence[0], manifest, evidence_root)
            except (ValueError, KeyError, TypeError, OSError, UnicodeError) as exc:
                errors.append(f'{case_id}: {exc}')
                continue
            passed += 1
        for missing in sorted(set(expected) - seen):
            errors.append(f'{missing}: missing case')
    except (ValueError, KeyError, TypeError, OSError) as exc:
        errors.append(str(exc))
    phase1_pass = not errors and total > 0 and passed == total
    if phase == 2:
        errors.append('gateway phase was removed from the active user objective')
    return {'schema_version': 1, 'requested_phase': phase, 'phase1_pass': phase1_pass,
            'gateway_unlocked': False, 'gateway_status': 'OUT_OF_SCOPE',
            'gate_pass': phase1_pass and phase == 1, 'passed_cases': passed, 'required_cases': total,
            'errors': errors}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='operation', required=True)
    inventory_parser = commands.add_parser('inventory', help='write a NOT_RUN manifest from installed entry points')
    inventory_parser.add_argument('--prefix', type=Path, required=True)
    inventory_parser.add_argument('--output', type=Path, required=True)
    verify = commands.add_parser('verify', help='validate the rmw_mdds/MDDS CLI and graph acceptance gate')
    verify.add_argument('--prefix', type=Path, required=True)
    verify.add_argument('--manifest', type=Path, required=True)
    verify.add_argument('--evidence-root', type=Path, required=True)
    verify.add_argument('--phase', type=int, choices=(1, 2), default=1)
    verify.add_argument('--report', type=Path)
    args = parser.parse_args(argv)
    try:
        inventory = discover_inventory(args.prefix)
        if args.operation == 'inventory':
            manifest = make_manifest(inventory)
            # Never overwrite a run manifest and its existing evidence references.
            with args.output.open('x', encoding='utf-8', newline='\n') as output:
                json.dump(manifest, output, indent=2, ensure_ascii=False)
                output.write('\n')
            print(f'INVENTORY commands={len(inventory["commands"])} cases={len(manifest["cases"])} status=NOT_RUN')
            return 0
        manifest = json.loads(args.manifest.read_text(encoding='utf-8'))
        result = validate_manifest(manifest, inventory, args.evidence_root, phase=args.phase)
        if args.report:
            args.report.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(result, indent=2))
        return 0 if result['gate_pass'] else 1
    except (ValueError, KeyError, TypeError, OSError, configparser.Error) as exc:
        print(f'ACCEPTANCE_ERROR: {exc}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
