#!/usr/bin/env python3
"""Real child wait and strict local-only broker acceptance metadata."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import sys


def mark_executable(path, expected_sha):
    if not re.fullmatch(r'[0-9a-f]{64}', expected_sha):
        raise ValueError('invalid frozen executable SHA256')
    if not hasattr(os, 'O_NOFOLLOW') or not hasattr(os, 'fchmod'):
        raise OSError('fd-bound executable mode requires O_NOFOLLOW and fchmod')
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | getattr(os, 'O_CLOEXEC', 0))
    try:
        original = os.fstat(fd)
        if (not stat.S_ISREG(original.st_mode) or original.st_nlink != 1 or
                not 0 < original.st_size <= 64 * 1024 * 1024):
            raise ValueError('executable must be a bounded, singly linked regular file')
        remaining, digest = original.st_size, hashlib.sha256()
        while remaining:
            data = os.read(fd, min(128 * 1024, remaining))
            if not data:
                raise ValueError('executable changed while hashing')
            digest.update(data)
            remaining -= len(data)
        if os.read(fd, 1) or digest.hexdigest() != expected_sha:
            raise ValueError('executable differs from frozen bytes')
        final = os.fstat(fd)
        if (final.st_dev, final.st_ino, final.st_size, final.st_mtime_ns, final.st_nlink) != (
                original.st_dev, original.st_ino, original.st_size, original.st_mtime_ns, 1):
            raise ValueError('executable inode changed before mode update')
        os.fchmod(fd, 0o700)
        return digest.hexdigest()
    finally:
        os.close(fd)


def validate_result(log, status, record, run_id, role, libdir, count, socket_path=''):
    errors = []
    if not isinstance(status, dict):
        return ['missing real child wait status']
    for key, expected in {'schema_version': 1, 'run_id': run_id, 'role': role,
                          'namespace': '/mdds_broker_' + run_id}.items():
        if status.get(key) != expected:
            errors.append(f'wrong terminal {key}')
    if type(status.get('returncode')) is not int or status['returncode'] != 0:
        errors.append('actual child did not exit with RC 0')
    pid, start = status.get('child_pid'), status.get('child_start')
    if (type(pid) is not int or pid <= 0 or not isinstance(start, str) or
            not re.fullmatch(r'[1-9][0-9]*', start)):
        errors.append('invalid child PID/start identity')
    expected_record = f'MDDS_OWNED_PROCESS RUN_ID={run_id} TAG={role}_child PID={pid} START={start}'
    if record.strip() != expected_record:
        errors.append('child ownership record differs from actual wait identity')

    def one_json(prefix):
        values = [line[len(prefix):] for line in log.splitlines() if line.startswith(prefix)]
        if len(values) != 1:
            errors.append(f'expected one {prefix.strip()} record')
            return None
        try:
            value = json.loads(values[0])
        except (ValueError, TypeError):
            errors.append(f'malformed {prefix.strip()} record')
            return None
        if not isinstance(value, dict):
            errors.append(f'non-object {prefix.strip()} record')
            return None
        return value

    if one_json('GRAPH_PROCESS_EXIT ') != status:
        errors.append('log and immutable real-wait terminal differ')
    if role == 'daemon':
        def key_values(prefix):
            lines = [line for line in log.splitlines() if line.startswith(prefix)]
            if len(lines) != 1:
                errors.append(f'expected one {prefix.strip()} record')
                return {}
            result = {}
            for item in lines[0][len(prefix):].split():
                if '=' not in item:
                    errors.append('malformed daemon metadata field')
                    continue
                key, value = item.split('=', 1)
                if key in result:
                    errors.append('duplicate daemon metadata field')
                result[key] = value
            return result
        ready, stopped = key_values('MDBC_LOCAL_READY '), key_values('MDBC_LOCAL_STOP ')
        for key, expected in {'mode': 'experimental-local-only', 'run_id': run_id,
                              'domain': '49', 'uid': '0', 'socket': socket_path,
                              'pid': str(pid)}.items():
            if ready.get(key) != expected:
                errors.append(f'wrong daemon READY {key}')
        for key, expected in {'mode': 'experimental-local-only', 'run_id': run_id,
                              'result': 'PASS', 'connections': '0', 'active_ports': '0'}.items():
            if stopped.get(key) != expected:
                errors.append(f'wrong daemon STOP {key}')
        return errors

    result = one_json('BROKER_LOCAL_ROS_RESULT ')
    if result is None:
        return errors
    if result.get('run_id') != run_id or result.get('verdict') != 'PASS':
        errors.append('probe run identity or verdict is not PASS')
    if result.get('physical_dsoftbus_proven') is not False:
        errors.append('local gate must not claim physical DSoftBus proof')

    def provenance(value):
        expected = {'pid': pid, 'libmdds_paths': [libdir + '/libmdds.so'],
                    'librmw_mdds_paths': [libdir + '/librmw_mdds.so'], 'owned_udp_sockets': []}
        if not isinstance(value, dict) or any(value.get(key) != want for key, want in expected.items()):
            errors.append('wrong loaded overlay/process identity or owned UDP socket exists')

    if role == 'contexts':
        if (result.get('mode') != 'contexts' or result.get('alpha_retired') is not True or
                result.get('beta_to_fresh_gamma') is not True or
                type(result.get('initial_samples_per_direction')) is not int or
                result['initial_samples_per_direction'] != count):
            errors.append('incomplete context graph/payload/retirement gate')
        provenance(result.get('before'))
        provenance(result.get('after'))
        expected_beta = [f'{run_id}:alpha:{n}' for n in range(count)] + [f'{run_id}:gamma:after_alpha_shutdown']
        if result.get('beta_received') != expected_beta:
            errors.append('beta payload sequence differs')
        if result.get('gamma_received') != [f'{run_id}:beta:after_alpha_shutdown']:
            errors.append('surviving context lost its broker path or replayed old history')
    elif role in ('alpha', 'beta'):
        if result.get('mode') != 'worker' or result.get('role') != role:
            errors.append('wrong worker mode or role')
        provenance(result.get('provenance'))
        peer = 'beta' if role == 'alpha' else 'alpha'
        if result.get('received') != [f'{run_id}:{peer}:{n}' for n in range(count)]:
            errors.append('worker payload sequence differs')
        if result.get('peer_retired') is not (role == 'beta'):
            errors.append('worker peer-retirement result differs')
        if result.get('completion_barrier') is not True or result.get('release_phase') != (
                'sent' if role == 'beta' else 'received'):
            errors.append('worker did not complete the explicit peer completion barrier')
    else:
        errors.append('unknown fixture role')
    return errors


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('operation', choices=('supervise', 'verify', 'pair', 'mark-executable'))
    parser.add_argument('--run-id', required=True)
    parser.add_argument('--fixture-role', choices=('contexts', 'alpha', 'beta', 'daemon'))
    parser.add_argument('--status-file')
    parser.add_argument('--child-record')
    parser.add_argument('--log')
    parser.add_argument('--libdir', default='')
    parser.add_argument('--socket', default='')
    parser.add_argument('--count', type=int, default=5)
    parser.add_argument('--alpha-status')
    parser.add_argument('--beta-status')
    parser.add_argument('--command', nargs=argparse.REMAINDER)
    parser.add_argument('--artifact')
    parser.add_argument('--sha256')
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_]{1,32}', args.run_id):
        parser.error('run id must contain 1..32 letters, digits or underscores')
    if args.operation == 'mark-executable':
        if not args.artifact or not args.sha256:
            parser.error('mark-executable requires artifact path and frozen SHA256')
        digest = mark_executable(args.artifact, args.sha256)
        print('BROKER_EXEC_READY sha256=' + digest, flush=True)
        return 0
    if args.operation == 'supervise':
        if not args.fixture_role or not args.command or not args.status_file or not args.child_record:
            parser.error('supervise requires role, status, child record and command')
        from board_graph_ownership import supervise_command
        return supervise_command(args.command, args.status_file, args.run_id, args.fixture_role,
                                 '/mdds_broker_' + args.run_id, args.child_record)
    if args.operation == 'pair':
        alpha = json.loads(Path(args.alpha_status).read_text(encoding='utf-8'))
        beta = json.loads(Path(args.beta_status).read_text(encoding='utf-8'))
        if (alpha.get('run_id') != args.run_id or beta.get('run_id') != args.run_id or
                alpha.get('role') != 'alpha' or beta.get('role') != 'beta' or
                alpha.get('child_pid') == beta.get('child_pid')):
            print('BROKER_PROCESS_PAIR FAIL', flush=True)
            return 1
        print('BROKER_PROCESS_PAIR PASS distinct_actual_child_pids=true', flush=True)
        return 0
    if not args.fixture_role or not args.status_file or not args.child_record or not args.log:
        parser.error('verify requires role, status, child record and log')
    errors = validate_result(Path(args.log).read_text(encoding='utf-8'),
                             json.loads(Path(args.status_file).read_text(encoding='utf-8')),
                             Path(args.child_record).read_text(encoding='utf-8'),
                             args.run_id, args.fixture_role, args.libdir, args.count, args.socket)
    print('BROKER_LOCAL_CASE ' + json.dumps({'run_id': args.run_id, 'role': args.fixture_role,
          'verdict': 'FAIL' if errors else 'PASS', 'errors': errors,
          'physical_dsoftbus_proven': False}, sort_keys=True), flush=True)
    return 1 if errors else 0


if __name__ == '__main__':
    raise SystemExit(main())
