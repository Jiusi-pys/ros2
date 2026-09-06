#!/usr/bin/env python3
"""Replay offline CLI captures and cryptographically verify public artifacts."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import shutil
import tarfile

import cli_acceptance as acceptance
from board_cli_metadata import write_json
from board_cli_offline import capture_marker, execution_text
from cli_offline_common import command_plan, check_packages, check_bag, check_security, expected_security
from verify_cli_metadata_archive import extract_checked, validate_process


def build_receipts(root, output, template, run_id, openssl):
    root, output = Path(root), Path(output)
    if template != acceptance.make_manifest(template['inventory']) or len(template['cases']) != 98:
        raise ValueError('the 98-case standard was modified')
    candidate = json.loads((root / 'candidates.json').read_text(encoding='utf-8'))
    if candidate.get('schema_version') != 1 or candidate.get('run_id') != run_id:
        raise ValueError('candidate belongs to a different run')
    context = json.loads(acceptance.read_artifact(candidate['oracle_context'], root))
    diagnostics = json.loads(acceptance.read_artifact(candidate['diagnostics'], root))
    if context.get('run_id') != run_id or context.get('inventory') != template['inventory']:
        raise ValueError('capture context/inventory differs from standard/run')
    board = acceptance.TARGET['board_serials'][0]
    if context.get('board_serial') != board or context.get('physical_dsoftbus_proven') is not False:
        raise ValueError('wrong board or non-offline scope')
    commands = command_plan(context['work'], run_id)
    expected_work = context['prefix'].rstrip('/') + '/.mdds-owned-runs/' + run_id + '/cli_metadata/work'
    if context['work'] != expected_work or context.get('commands') != commands:
        raise ValueError('command paths/plan escaped their run-owned work directory')
    if [case['id'] for case in candidate['cases']] != list(commands):
        raise ValueError('offline candidate cases missing/duplicated/reordered')
    rmw = diagnostics['rmw']
    if rmw.get('returncode') != 0 or rmw.get('stdout', '').strip() != 'rmw_mdds':
        raise ValueError('actual RMW identifier diagnostic failed')
    output.mkdir(mode=0o700)
    # Copy only bounded public flat evidence; original board logs remain exact.
    for path in root.iterdir():
        if not path.is_file() or path.is_symlink(): raise ValueError('unexpected extracted artifact')
        shutil.copyfile(path, output / path.name)
    signature_root = output / 'host_signature_checks'; signature_root.mkdir(mode=0o700)
    manifest = copy.deepcopy(template); manifest['run_id'] = run_id
    cases = {case['id']: case for case in manifest['cases']}
    for case in manifest['cases']: case['status'], case['evidence'] = 'NOT_RUN', []
    failed = {}
    for item in candidate['cases']:
        case_id = item['id']; case = cases[case_id]
        errors, proofs = [], []
        capture = json.loads(acceptance.read_artifact(item['capture'], root))
        marker = capture_marker(run_id, case_id, item['capture']['sha256'])
        try:
            observations = item['executions']
            if [o['argv'] for o in observations] != commands[case_id]:
                raise ValueError('actual CLI argv differs from the exact plan')
            help_result = diagnostics['help'][case_id]
            if (help_result.get('returncode') != 0 or help_result.get('timed_out') is not False or
                    help_result.get('argv') != ['ros2'] + case_id.removeprefix('cli:').split('/') + ['--help']):
                raise ValueError('actual CLI help/argument registration unavailable (not a functional pass)')
            for index, observation in enumerate(observations):
                if (type(observation.get('returncode')) is not int or observation['returncode'] != 0 or
                        observation.get('timed_out') is not False or observation.get('board_serial') != board):
                    raise ValueError('real CLI operation failed or timed out')
                expected_actual = [context['python_executable'], '-B', '-c',
                    'from ros2cli.cli import main; raise SystemExit(main())'] + observation['argv'][1:]
                if observation.get('actual_argv') != expected_actual: raise ValueError('wrong actual CLI interpreter argv')
                raw = acceptance.read_artifact(observation['log'], root).decode('utf-8')
                if raw != execution_text(observation, run_id, case_id, board, marker if index == 0 else ''):
                    raise ValueError('raw board stdout/stderr/terminal/capture marker mismatch')
            if capture.get('errors'): raise ValueError('board artifact capture failed: ' + '; '.join(capture['errors']))
            if case_id == 'cli:pkg/create': check_packages(capture, context)
            elif case_id == 'cli:bag/list':
                if context.get('bag_errors'): raise ValueError('; '.join(context['bag_errors']))
                check_bag(observations, context)
            else:
                identities, custom = expected_security(case_id, run_id)
                proofs = check_security(capture, identities, custom, context['domain'], openssl, signature_root)
                if case_id in ('cli:security/list_enclaves', 'cli:security/list_keys'):
                    if observations[0]['stdout'].splitlines() != sorted(identities):
                        raise ValueError('listed enclave identities differ from exact scratch keystore')
        except Exception as exc:
            errors.append(type(exc).__name__ + ': ' + str(exc))
        passed = not errors
        if not passed: failed[case_id] = errors
        token = case_id.replace(':', '_').replace('/', '_')
        proof_ref = write_json(output / (token + '.host_oracle.json'), {
            'schema_version': 1, 'run_id': run_id, 'case_id': case_id,
            'passed': passed, 'errors': errors, 'host_signature_checks': proofs,
            'openssl_path': str(openssl), 'openssl_sha256': hashlib.sha256(Path(openssl).read_bytes()).hexdigest(),
            'artifact_capture': item['capture'], 'private_material_exported': False})
        case['status'] = 'PASS' if passed else 'FAIL'
        executions = [{key: observation[key] for key in
                       ('argv', 'actual_argv', 'returncode', 'board_serial', 'timed_out', 'log')}
                      for observation in item['executions']]
        receipt = {'schema_version': 1, 'run_id': run_id, 'case_id': case_id,
                   'kind': 'functional', 'status': case['status'], 'board_serials': acceptance.TARGET['board_serials'],
                   'rmw_implementation': 'rmw_mdds', 'transport': 'dsoftbus',
                   'transport_exercised': False, 'scope': 'offline-files-and-metadata',
                   'oracle_context': candidate['oracle_context'], 'artifact_capture': item['capture'],
                   'host_oracle': proof_ref, 'executions': executions, 'errors': errors,
                   'assertions': [{'id': name, 'passed': passed, 'execution': 0,
                                   'pattern': marker if passed else ''} for name in case['assertions']]}
        case['evidence'] = [write_json(output / (token + '.receipt.json'), receipt)]
        if passed: acceptance.validate_receipt(case, case['evidence'][0], manifest, output)
    write_json(output / 'partial_manifest.json', manifest)
    gate = acceptance.validate_manifest(manifest, template['inventory'], output, phase=2)
    if gate['phase1_pass'] or gate['gateway_unlocked']: raise ValueError('partial offline batch cannot unlock the global gate')
    write_json(output / 'phase_gate.json', gate)
    summary = {'selected_cases': 9, 'passed_cases': 9 - len(failed), 'failed_cases': failed,
               'phase1_pass': False, 'gateway_unlocked': False, 'physical_dsoftbus_proven': False}
    write_json(output / 'summary.json', summary)
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('archive', 'archive-record', 'status', 'child-record', 'log', 'template', 'destination', 'report', 'openssl'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--archive-sha256', required=True)
    parser.add_argument('--run-id', required=True)
    args = parser.parse_args()
    errors, summary = [], {}
    try:
        if not args.openssl.is_file() or args.openssl.is_symlink(): raise ValueError('host OpenSSL verifier unavailable')
        status = json.loads(args.status.read_text())
        errors.extend(validate_process(status, args.child_record.read_text(), args.log.read_text(encoding='utf-8'), args.run_id))
        if errors: raise ValueError('capture supervisor did not finish with a valid real process exit')
        record = json.loads(args.archive_record.read_text())
        if (record.get('schema_version') != 1 or record.get('run_id') != args.run_id or record.get('ok') is not True or
                record.get('metadata_returncode') != status.get('returncode') or record.get('sha256') != args.archive_sha256 or
                type(record.get('bytes')) is not int or record['bytes'] != args.archive.stat().st_size):
            raise ValueError('archive is not bound to actual process/run/byte count')
        captured = args.destination.with_name(args.destination.name + '_capture')
        extract_checked(args.archive, args.archive_sha256, captured)
        if record.get('members') != len(list(captured.iterdir())): raise ValueError('archive member count mismatch')
        summary = build_receipts(captured, args.destination, json.loads(args.template.read_text()), args.run_id, args.openssl.resolve())
        if summary['passed_cases'] != 9: errors.append('one or more offline CLI operations failed functional verification')
    except Exception as exc:
        errors.append(type(exc).__name__ + ': ' + str(exc))
    result = {'run_id': args.run_id, 'batch_pass': not errors, 'summary': summary, 'errors': errors,
              'phase1_pass': False, 'gateway_unlocked': False, 'archive_sha256': args.archive_sha256}
    write_json(args.report, result)
    print(json.dumps(result, sort_keys=True))
    return 1 if errors else 0

if __name__ == '__main__': raise SystemExit(main())
