#!/usr/bin/env python3
"""Capture nine real offline ROS CLI operations; host artifacts decide PASS."""
import sys
sys.dont_write_bytecode = True
import argparse
import json
import os
from pathlib import Path
import re

import cli_acceptance as acceptance
from board_cli_metadata import execute, claim_output, write_json
from cli_offline_common import command_plan, identity_root, policy_xml, capture_tree, collect_plugins


def capture_marker(run_id, case_id, capture_sha):
    return f'MDDS_CLI_OFFLINE_CAPTURE RUN_ID={run_id} CASE={case_id} SHA256={capture_sha}'


def execution_text(observation, run_id, case_id, board, marker=''):
    text = ('MDDS_CLI_ACTUAL_ARGV ' + json.dumps(observation['actual_argv']) + '\n' +
            'MDDS_CLI_STDOUT_BEGIN\n' + observation['stdout'] + '\nMDDS_CLI_STDOUT_END\n' +
            'MDDS_CLI_STDERR_BEGIN\n' + observation['stderr'] + '\nMDDS_CLI_STDERR_END\n' +
            acceptance.terminal_marker(run_id, case_id, observation['returncode'], observation['argv'], board) + '\n')
    return text + (marker + '\n' if marker else '')


def run(prefix, run_id, board, output):
    os.umask(0o077)
    output = claim_output(prefix, run_id, output)
    work = output / 'work'; work.mkdir(mode=0o700)
    environment = {'AMENT_PREFIX_PATH': str(prefix), 'ROS_DOMAIN_ID': '53',
                   'PYTHONDONTWRITEBYTECODE': '1', 'PYTHONWARNINGS': 'default'}
    for key, leaf in {'HOME': 'home', 'ROS_HOME': 'ros_home', 'XDG_CONFIG_HOME': 'config',
                      'XDG_CACHE_HOME': 'cache', 'TMPDIR': 'tmp'}.items():
        path = work / leaf; path.mkdir(mode=0o700); environment[key] = str(path)
    (work / 'permission_policy.xml').write_bytes(policy_xml([identity_root(run_id) + '/alpha']))
    (work / 'artifacts_policy.xml').write_bytes(policy_xml([identity_root(run_id) + '/gamma', identity_root(run_id) + '/delta']))
    template = json.loads(Path(__file__).with_name('cli_acceptance_manifest.json').read_text())
    inventory = acceptance.discover_inventory(prefix)
    if template != acceptance.make_manifest(inventory):
        raise ValueError('installed CLI inventory differs from the unchanged 98-case standard')
    plan = command_plan(work, run_id)
    if not set(plan) <= {case['id'] for case in template['cases']}:
        raise ValueError('offline plan contains an unknown standard case')
    context = {'schema_version': 1, 'run_id': run_id, 'board_serial': board,
               'prefix': str(prefix), 'work': str(work), 'domain': 53, 'commands': plan,
               'inventory': inventory, 'python_executable': sys.executable,
               'scope': 'offline-files-and-metadata', 'physical_dsoftbus_proven': False}
    try:
        context['bag_plugins'], context['bag_sources'] = collect_plugins(prefix)
        context['bag_errors'] = []
    except Exception as exc:
        context.update(bag_plugins={}, bag_sources=[], bag_errors=[str(exc)])
    diagnostics = {'help': {}, 'rmw': execute([sys.executable, '-B', '-c',
        "from rclpy.utilities import get_rmw_implementation_identifier; print(get_rmw_implementation_identifier())"], environment, 40)}
    for case_id in plan:
        diagnostics['help'][case_id] = execute(['ros2'] + case_id.removeprefix('cli:').split('/') + ['--help'], environment, 40)
    context_ref = write_json(output / 'oracle_context.json', context)
    diagnostics_ref = write_json(output / 'diagnostics.json', diagnostics)
    candidates = []
    os.chdir(work)
    for case_id, commands in plan.items():
        observations = []
        for argv in commands:
            try: observations.append(execute(argv, environment, 60))
            except OSError as exc:
                observations.append({'argv': argv, 'actual_argv': [], 'returncode': 127,
                                     'stdout': '', 'stderr': str(exc), 'timed_out': False})
        capture = {'files': {}, 'keys': {}, 'errors': []}
        try:
            if case_id == 'cli:pkg/create': capture.update(capture_tree(work / 'packages'))
            elif case_id.startswith('cli:security/'):
                leaf = 'generated_keystore' if case_id.endswith('/generate_artifacts') else 'keystore'
                capture.update(capture_tree(work / leaf, security=True))
        except Exception as exc:
            capture['errors'].append(str(exc))
        token = case_id.replace(':', '_').replace('/', '_')
        capture_ref = write_json(output / (token + '.capture.json'), capture)
        marker = capture_marker(run_id, case_id, capture_ref['sha256'])
        records = []
        for index, observation in enumerate(observations):
            path = output / f'{token}.{index}.log'
            path.write_text(execution_text(observation, run_id, case_id, board, marker if index == 0 else ''),
                            encoding='utf-8', newline='\n')
            records.append(dict(observation, board_serial=board,
                                log={'path': path.name, 'sha256': acceptance.digest(path.read_bytes())}))
        candidates.append({'id': case_id, 'executions': records, 'capture': capture_ref})
    write_json(output / 'candidates.json', {'schema_version': 1, 'run_id': run_id,
        'oracle_context': context_ref, 'diagnostics': diagnostics_ref, 'cases': candidates})
    # This is capture completion, never a claim that generation/signatures pass.
    print('CLI_OFFLINE_CAPTURE_COMPLETE ' + json.dumps({'run_id': run_id, 'cases': len(candidates),
          'physical_dsoftbus_proven': False}, sort_keys=True), flush=True)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', type=Path, required=True)
    parser.add_argument('--run-id', required=True)
    parser.add_argument('--board-serial', choices=acceptance.TARGET['board_serials'][:1], required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    profile = {'RMW_IMPLEMENTATION': 'rmw_mdds', 'MDDS_DEPLOYMENT_PROFILE': 'ohos_dsoftbus',
               'ROS_AUTOMATIC_DISCOVERY_RANGE': 'SYSTEM_DEFAULT'}
    if any(os.environ.get(k) != v for k, v in profile.items()) or 'MDDS_TRANSPORT' in os.environ:
        raise ValueError('parent must source the OHOS DSoftBus-only profile')
    return run(args.prefix.resolve(), args.run_id, args.board_serial, args.output_dir.absolute())

if __name__ == '__main__': raise SystemExit(main())
