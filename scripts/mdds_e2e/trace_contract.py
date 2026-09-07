"""Independent decoded CTF contract for tracing lifecycle observations."""
import re

PHASES = ('active', 'paused', 'resumed', 'stopped', 'interactive')


def recipe(run, nonce):
    path = '/data/local/tmp/ros2/.mdds-owned-runs/' + run + '/trace_probe/traces'
    session = 'lifecycle_' + nonce[:12]
    options = ['--path', path, '--ust', 'ros2:rcl_node_init', '--context', 'vpid']
    rows = []
    for verb in ('start', 'pause', 'resume', 'stop'):
        args = ['trace', verb, session] + (options if verb == 'start' else [])
        rows.append(('cli:trace/' + verb, 'trace_' + verb, args, {'session': session}))
    session = 'interactive_' + nonce[:12]
    rows.append(('cli:trace', 'trace_interactive', ['trace', '--session-name', session] + options, {'session': session}))
    return rows


def validate_report(value, run, nonce, role, runtime_sha):
    identity = {'run_id': run, 'nonce': nonce, 'role': role, 'passed': True}
    if any(value.get(k) != v for k, v in identity.items()): raise ValueError('tracing identity/status differs')
    ns = value.get('namespace')
    if not isinstance(ns, str) or not re.fullmatch(r'mnt:\[[0-9]+\]', ns) or ns == value.get('system_namespace'):
        raise ValueError('tracing namespace is not isolated')
    if not re.fullmatch(r'mnt:\[[0-9]+\]', str(value.get('system_namespace'))): raise ValueError('missing system namespace')
    if value.get('cleanup_remaining') != [] or not value.get('namespace_cleanup'): raise ValueError('tracing cleanup incomplete')
    before = value.get('runtime_before', {})
    if before.get('manifest_sha256') != runtime_sha or before.get('files_verified', 0) < 20 or value.get('runtime_after') != before:
        raise ValueError('tracing runtime was not frozen and reverified')
    commands = value.get('commands', [])
    if len(commands) != 5: raise ValueError('tracing commands incomplete')
    for command, (case, label, args, _) in zip(commands, recipe(run, nonce)):
        if command.get('label') != label.removeprefix('trace_') or command.get('returncode') != 0:
            raise ValueError('tracing command failed or reordered')
        actual = command.get('argv', [])
        if actual != ['/data/python312-rk3588a/usr/bin/python3.12', '-u', '-B', '-c',
                      'from ros2cli.cli import main; raise SystemExit(main())'] + args:
            raise ValueError('wrong actual tracing command')
        if type(command.get('pid')) is not int or command['pid'] <= 0 or not str(command.get('start')).isdecimal():
            raise ValueError('missing tracing process identity')
    actors = value.get('actors', {})
    if set(actors) != set(PHASES): raise ValueError('trace actors incomplete')
    peer = 'B' if role == 'A' else 'A'
    for index, phase in enumerate(PHASES):
        actor = actors[phase]
        expected = {'node': 'trace_' + phase + '_' + role, 'mount_namespace': ns,
                    'service': '/ros_broker_' + run + '/' + peer + '/alpha/serve',
                    'a': int(nonce[:7], 16), 'b': index + 1,
                    'sum': int(nonce[:7], 16) + index + 1, 'payload': '|'.join((run, nonce, role, phase)), 'peer_ack': True}
        if any(actor.get(k) != v for k, v in expected.items()): raise ValueError('trace actor or peer payload differs')
        if type(actor.get('pid')) is not int or actor['pid'] <= 0 or not str(actor.get('start')).isdecimal():
            raise ValueError('trace actor process identity missing')
        if not any('libtracetools' in p for p in actor.get('trace_mappings', {})):
            raise ValueError('actor tracer mapping missing')


def validate_events(decoded, actors):
    if set(decoded) != {'lifecycle', 'interactive'} or set(actors) != {
            'active', 'paused', 'resumed', 'stopped', 'interactive'}:
        raise ValueError('incomplete trace sessions/actors')
    for session, raw in decoded.items():
        observed = []
        for line in raw.splitlines():
            if 'ros2:rcl_node_init:' not in line:
                continue
            name = re.search(r'\bnode_name = "([^"]+)"', line)
            pid = re.search(r'\bvpid = ([0-9]+)\b', line)
            if not name or not pid:
                raise ValueError('node trace lacks name or process identity')
            observed.append((name[1], int(pid[1])))
        phases = ('active', 'resumed') if session == 'lifecycle' else ('interactive',)
        expected = [(actors[phase]['node'], actors[phase]['pid']) for phase in phases]
        if sorted(observed) != sorted(expected):
            raise ValueError(f'{session} trace events differ: {observed}; expected {expected}')
