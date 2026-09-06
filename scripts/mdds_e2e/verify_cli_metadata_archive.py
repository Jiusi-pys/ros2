#!/usr/bin/env python3
"""Verify the real metadata-process exit and a bounded flat evidence archive."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
import tarfile

import board_cli_metadata as metadata
import cli_acceptance as acceptance


MAX_ARCHIVE_BYTES = 16 * 1024 * 1024
MAX_MEMBER_BYTES = 4 * 1024 * 1024
MAX_MEMBERS = 128


def validate_process(status, record, log, run_id):
    if not isinstance(status, dict):
        return ['metadata process has no valid terminal object']
    errors = []
    for field, expected in {'schema_version': 1, 'run_id': run_id,
                            'role': 'metadata', 'namespace': '/cli_metadata'}.items():
        if status.get(field) != expected:
            errors.append(f'wrong metadata terminal {field}')
    if type(status.get('returncode')) is not int or status['returncode'] != 0:
        errors.append('metadata subprocess did not exit with RC 0')
    pid, start = status.get('child_pid'), status.get('child_start')
    if type(pid) is not int or pid <= 0 or not isinstance(start, str) or not re.fullmatch(r'[1-9][0-9]*', start):
        errors.append('metadata child PID/start identity is invalid')
    expected_record = f'MDDS_OWNED_PROCESS RUN_ID={run_id} TAG=metadata_child PID={pid} START={start}'
    if record.strip() != expected_record:
        errors.append('metadata child ownership record does not match its terminal')
    terminals = [line[len('GRAPH_PROCESS_EXIT '):] for line in log.splitlines()
                 if line.startswith('GRAPH_PROCESS_EXIT ')]
    try:
        if len(terminals) != 1 or json.loads(terminals[0]) != status:
            errors.append('metadata log lacks one matching real-wait terminal')
    except ValueError:
        errors.append('metadata log terminal is malformed')
    return errors


def extract_checked(archive_path, expected_sha, destination, *,
                    max_bytes=MAX_ARCHIVE_BYTES, max_member_bytes=MAX_MEMBER_BYTES,
                    max_members=MAX_MEMBERS):
    archive_path, destination = Path(archive_path), Path(destination)
    if (archive_path.is_symlink() or not archive_path.is_file() or
            not 0 < archive_path.stat().st_size <= max_bytes):
        raise ValueError('archive is missing, linked, empty or exceeds the byte limit')
    if not isinstance(expected_sha, str) or not re.fullmatch(r'[0-9a-f]{64}', expected_sha):
        raise ValueError('invalid expected archive SHA256')
    with archive_path.open('rb') as source:
        if hashlib.file_digest(source, 'sha256').hexdigest() != expected_sha:
            raise ValueError('archive SHA256 mismatch')
    if destination.exists() or destination.is_symlink():
        raise ValueError('refusing to reuse extraction destination')
    with tarfile.open(archive_path, mode='r:') as archive:
        members, seen, total = [], set(), 0
        for member in archive:
            name = member.name
            if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]*\.(?:json|log)', name):
                raise ValueError(f'archive member is not a safe flat JSON/log name: {name}')
            if name.split('.', 1)[0].upper() in {'CON', 'PRN', 'AUX', 'NUL'} or re.fullmatch(
                    r'(?:COM|LPT)[1-9]', name.split('.', 1)[0].upper()):
                raise ValueError(f'archive member is a reserved device name: {name}')
            if (member.type not in (tarfile.REGTYPE, tarfile.AREGTYPE) or member.sparse is not None or
                    not 0 <= member.size <= max_member_bytes or name.casefold() in seen):
                raise ValueError(f'linked, special, duplicate or oversized archive member: {name}')
            total += member.size
            if total > max_bytes or len(members) >= max_members:
                raise ValueError('archive expanded byte/member limit exceeded')
            seen.add(name.casefold())
            members.append(member)
        if not members:
            raise ValueError('empty evidence archive')
        # No archive member is written until the complete inventory has passed.
        destination.mkdir(mode=0o700)
        for member in members:
            source = archive.extractfile(member)
            if source is None:
                raise ValueError(f'archive has no regular member data: {member.name}')
            with source:
                data = source.read(member.size + 1)
            if len(data) != member.size:
                raise ValueError(f'truncated archive member: {member.name}')
            with (destination / member.name).open('xb') as output:
                output.write(data)


def validate_batch(root, template, run_id):
    errors, passed = [], 0
    root = Path(root)
    selected = set(metadata.CASE_COMMANDS)
    expected_files = {'partial_manifest.json', 'oracle_context.json', 'summary.json', 'phase_gate.json'}
    try:
        if template != acceptance.make_manifest(template['inventory']) or len(template['cases']) != 98:
            raise ValueError('template differs from the unmodified 98-case standard')
        manifest = json.loads((root / 'partial_manifest.json').read_text(encoding='utf-8'))
        if not isinstance(manifest, dict):
            raise ValueError('partial manifest is not an object')
        if manifest.get('run_id') != run_id or manifest.get('inventory') != template['inventory']:
            raise ValueError('partial manifest run/inventory mismatch')
        cases = manifest['cases']
        expected_ids = [case['id'] for case in template['cases']]
        if (not isinstance(cases, list) or not all(isinstance(case, dict) for case in cases) or
                [case.get('id') for case in cases] != expected_ids):
            raise ValueError('partial manifest case inventory is missing, duplicated or reordered')
        for case in cases:
            if case['id'] in selected:
                if case.get('status') != 'PASS':
                    errors.append(f'{case["id"]} did not pass')
                reference = case['evidence'][0]
                receipt = json.loads(acceptance.read_artifact(reference, root))
                if not isinstance(receipt, dict):
                    raise ValueError('receipt is not an object')
                expected_files.add(reference['path'])
                context_ref = receipt.get('oracle_context')
                if not isinstance(context_ref, dict) or context_ref.get('path') != 'oracle_context.json':
                    raise ValueError('receipt does not reference the shared oracle context')
                acceptance.read_artifact(context_ref, root)
                for execution in receipt['executions']:
                    expected_files.add(execution['log']['path'])
            elif case.get('status') != 'NOT_RUN' or case.get('evidence') != []:
                errors.append(f'{case["id"]}: this batch must retain NOT_RUN with no evidence')
        gate = acceptance.validate_manifest(manifest, template['inventory'], root, phase=2)
        passed = gate['passed_cases']
        expected_gate_errors = [f"{case_id}: status 'NOT_RUN' is not PASS"
                                for case_id in expected_ids if case_id not in selected]
        if (passed != 12 or gate['required_cases'] != 98 or
                sorted(gate['errors']) != sorted(expected_gate_errors) or
                gate['phase1_pass'] is not False or gate['gateway_unlocked'] is not False):
            errors.append('original acceptance verifier did not report exactly 12 PASS and 86 NOT_RUN')
            errors.extend(error for error in gate['errors'] if error not in expected_gate_errors)
        saved_gate = json.loads((root / 'phase_gate.json').read_text(encoding='utf-8'))
        if saved_gate != gate:
            errors.append('board phase-gate report differs from the host replay')
        summary = json.loads((root / 'summary.json').read_text(encoding='utf-8'))
        if not isinstance(summary, dict):
            raise ValueError('summary is not an object')
        for field, expected in {'selected_cases': 12, 'passed_cases': 12, 'failed_cases': [],
                                'phase1_pass': False, 'gateway_unlocked': False}.items():
            if summary.get(field) != expected:
                errors.append(f'board summary {field} disagrees with the receipts')
        actual_files = {path.name for path in root.iterdir() if path.is_file() and not path.is_symlink()}
        if actual_files != expected_files or any(not path.is_file() or path.is_symlink() for path in root.iterdir()):
            errors.append('archive contains missing or unreferenced evidence members')
    except (ValueError, KeyError, TypeError, IndexError, OSError) as exc:
        errors.append(str(exc))
    return {'errors': errors, 'passed_cases': passed, 'phase1_pass': False, 'gateway_unlocked': False}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('archive', 'archive-record', 'status', 'child-record', 'log', 'template', 'destination', 'report'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--archive-sha256', required=True)
    parser.add_argument('--run-id', required=True)
    args = parser.parse_args(argv)
    errors, passed = [], 0
    try:
        status = json.loads(args.status.read_text(encoding='utf-8'))
        errors.extend(validate_process(status, args.child_record.read_text(encoding='utf-8'),
                                       args.log.read_text(encoding='utf-8'), args.run_id))
        package = json.loads(args.archive_record.read_text(encoding='utf-8'))
        if (not isinstance(package, dict) or package.get('schema_version') != 1 or
                package.get('run_id') != args.run_id or package.get('ok') is not True or
                package.get('metadata_returncode') != status.get('returncode') or
                package.get('sha256') != args.archive_sha256 or
                type(package.get('bytes')) is not int or package['bytes'] != args.archive.stat().st_size):
            raise ValueError('archive packaging record does not match this process/run/archive')
        extract_checked(args.archive, args.archive_sha256, args.destination)
        if package.get('members') != len(list(args.destination.iterdir())):
            raise ValueError('archive packaging record member count mismatch')
        result = validate_batch(args.destination, json.loads(args.template.read_text(encoding='utf-8')), args.run_id)
        errors.extend(result['errors'])
        passed = result['passed_cases']
    except (ValueError, KeyError, TypeError, OSError, tarfile.TarError) as exc:
        errors.append(str(exc))
    report = {'batch_pass': not errors, 'run_id': args.run_id, 'passed_cases': passed,
              'phase1_pass': False, 'gateway_unlocked': False,
              'archive_sha256': args.archive_sha256, 'errors': errors}
    metadata.write_json(args.report, report)
    print(json.dumps(report, sort_keys=True))
    return 0 if report['batch_pass'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
