#!/usr/bin/env python3
"""Fault injection for host-side metadata batch acceptance; no board access."""

import copy
import io
import json
from pathlib import Path
import shutil
import tarfile
import tempfile
import unittest

import board_cli_metadata as metadata
import cli_acceptance as acceptance
import test_board_cli_metadata as oracle_fixtures
import verify_cli_metadata_archive as verifier
import board_cli_metadata_supervisor as supervisor


class MetadataArchiveTest(unittest.TestCase):
    def setUp(self):
        self.fixture = oracle_fixtures.MetadataOracleTest()
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.root = self.fixture.root
        self.source = self.root / 'source'
        self.source.mkdir()
        self.run_id = 'metadata_archive_test'
        self.template = json.loads(Path(__file__).with_name('cli_acceptance_manifest.json').read_text())

        def execute(argv, env, timeout):
            for case_id in metadata.CASE_COMMANDS:
                for value in self.fixture.outputs(case_id):
                    if value['argv'] == argv:
                        return {**value, 'actual_argv': argv, 'timed_out': False}
            raise AssertionError(argv)

        self.manifest = metadata.run_cases(
            self.template, self.fixture.context, self.source, self.run_id,
            acceptance.TARGET['board_serials'][0], {}, executor=execute)
        gate = acceptance.validate_manifest(self.manifest, self.template['inventory'], self.source, phase=2)
        metadata.write_json(self.source / 'phase_gate.json', gate)
        metadata.write_json(self.source / 'summary.json', {
            'selected_cases': 12, 'passed_cases': 12, 'failed_cases': [],
            'phase1_pass': False, 'gateway_unlocked': False, 'output_dir': '/unused'})
        self.status = {'schema_version': 1, 'run_id': self.run_id, 'role': 'metadata',
                       'namespace': '/cli_metadata', 'returncode': 0,
                       'child_pid': 12, 'child_start': '34'}
        self.record = f'MDDS_OWNED_PROCESS RUN_ID={self.run_id} TAG=metadata_child PID=12 START=34\n'
        self.log = 'GRAPH_PROCESS_EXIT ' + json.dumps(self.status, sort_keys=True) + '\n'

    def archive(self, missing=None, extra=None):
        path = self.root / f'archive-{len(list(self.root.glob("archive-*")))}.tar'
        with tarfile.open(path, 'w') as archive:
            for source in sorted(self.source.iterdir()):
                if source.is_file() and source.name != missing:
                    archive.add(source, arcname=source.name)
            if extra:
                info, data = extra
                archive.addfile(info, io.BytesIO(data) if data is not None else None)
        return path

    def test_real_process_terminal_accepts_only_exact_zero_exit_and_identity(self):
        self.assertEqual([], verifier.validate_process(self.status, self.record, self.log, self.run_id))
        for field, value in [('returncode', 1), ('returncode', 139), ('returncode', False),
                             ('run_id', 'old'), ('child_pid', 13), ('child_start', '35')]:
            status = {**self.status, field: value}
            self.assertTrue(verifier.validate_process(status, self.record, self.log, self.run_id))
        self.assertTrue(verifier.validate_process(self.status, self.record, '', self.run_id))
        self.assertTrue(verifier.validate_process(self.status, self.record, self.log * 2, self.run_id))

    def test_exact_archive_replays_original_gate_as_twelve_pass_eighty_six_not_run(self):
        archive = self.archive()
        extracted = self.root / 'extracted'
        verifier.extract_checked(archive, metadata.sha(archive.read_bytes()), extracted)
        result = verifier.validate_batch(extracted, self.template, self.run_id)
        self.assertEqual([], result['errors'])
        self.assertEqual(12, result['passed_cases'])
        self.assertFalse(result['phase1_pass'])
        self.assertFalse(result['gateway_unlocked'])

    def test_missing_receipt_cannot_pass_even_with_a_success_summary(self):
        archive = self.archive(missing='cli_pkg_list.receipt.json')
        extracted = self.root / 'missing_receipt'
        verifier.extract_checked(archive, metadata.sha(archive.read_bytes()), extracted)
        self.assertTrue(verifier.validate_batch(extracted, self.template, self.run_id)['errors'])

    def test_changed_archive_hash_is_rejected_before_extraction(self):
        archive = self.archive()
        destination = self.root / 'hash_rejected'
        with self.assertRaises(ValueError):
            verifier.extract_checked(archive, '0' * 64, destination)
        self.assertFalse(destination.exists())

    def test_changed_log_and_false_partial_status_fail_receipt_validation(self):
        log = self.source / 'cli_pkg_list.0.log'
        log.write_text(log.read_text() + 'changed bytes\n')
        self.assertTrue(verifier.validate_batch(self.source, self.template, self.run_id)['errors'])

    def test_malformed_nested_manifest_is_reported_as_failed_evidence(self):
        changed = copy.deepcopy(self.manifest)
        changed['cases'][0] = None
        (self.source / 'partial_manifest.json').write_text(json.dumps(changed))
        self.assertTrue(verifier.validate_batch(self.source, self.template, self.run_id)['errors'])
        changed = copy.deepcopy(self.manifest)
        next(case for case in changed['cases'] if case['id'] == 'cli:action/send_goal')['status'] = 'PASS'
        (self.source / 'partial_manifest.json').write_text(json.dumps(changed))
        self.assertTrue(verifier.validate_batch(self.source, self.template, self.run_id)['errors'])

    def test_tar_rejects_escape_nested_links_devices_duplicates_and_oversized_members(self):
        for name, kind, size in [
            ('../escape.json', tarfile.REGTYPE, 1), ('nested/file.json', tarfile.REGTYPE, 1),
            ('link.json', tarfile.SYMTYPE, 0), ('hard.json', tarfile.LNKTYPE, 0),
            ('device.log', tarfile.CHRTYPE, 0), ('CON.json', tarfile.REGTYPE, 1),
            ('summary.json', tarfile.REGTYPE, 1), ('large.log', tarfile.REGTYPE, 4194305),
        ]:
            with self.subTest(name=name):
                info = tarfile.TarInfo(name)
                info.type, info.size = kind, size
                if kind in (tarfile.SYMTYPE, tarfile.LNKTYPE):
                    info.linkname = 'summary.json'
                archive = self.archive(extra=(info, b'x' * size if kind == tarfile.REGTYPE else None))
                destination = self.root / ('invalid_' + str(len(list(self.root.glob('archive-*')))))
                with self.assertRaises(ValueError):
                    verifier.extract_checked(archive, metadata.sha(archive.read_bytes()), destination)
                self.assertFalse(destination.exists())

    def test_tar_member_count_limit_and_existing_destination_are_rejected(self):
        archive = self.archive()
        with self.assertRaises(ValueError):
            verifier.extract_checked(archive, metadata.sha(archive.read_bytes()), self.root / 'count', max_members=1)
        existing = self.root / 'existing'
        existing.mkdir()
        with self.assertRaises((ValueError, FileExistsError)):
            verifier.extract_checked(archive, metadata.sha(archive.read_bytes()), existing)

    def test_supervisor_packages_only_complete_flat_evidence_and_hashes_exact_bytes(self):
        run_root = self.root / 'packaged_run'
        evidence = run_root / 'cli_metadata'
        evidence.mkdir(parents=True)
        for path in self.source.iterdir():
            shutil.copyfile(path, evidence / path.name)
        (evidence / 'cache').mkdir()
        record = supervisor.package_evidence(run_root)
        archive = run_root / 'cli_metadata.tar'
        self.assertEqual(metadata.sha(archive.read_bytes()), record['sha256'])
        self.assertEqual(archive.stat().st_size, record['bytes'])
        destination = self.root / 'packaged_extracted'
        verifier.extract_checked(archive, record['sha256'], destination)
        self.assertEqual(record['members'], len(list(destination.iterdir())))
        self.assertEqual([], verifier.validate_batch(destination, self.template, self.run_id)['errors'])

    def test_host_command_rejects_nonzero_process_even_when_all_receipts_pass(self):
        archive = self.archive()
        status = {**self.status, 'returncode': 7}
        for name, value in [('status.json', status), ('template.json', self.template),
                            ('package.json', {'schema_version': 1, 'run_id': self.run_id, 'ok': True,
                                              'metadata_returncode': 7, 'sha256': metadata.sha(archive.read_bytes()),
                                              'bytes': archive.stat().st_size,
                                              'members': len(list(self.source.iterdir()))})]:
            (self.root / name).write_text(json.dumps(value))
        (self.root / 'child.pid').write_text(self.record)
        (self.root / 'metadata.log').write_text('GRAPH_PROCESS_EXIT ' + json.dumps(status, sort_keys=True) + '\n')
        rc = verifier.main([
            '--archive', str(archive), '--archive-sha256', metadata.sha(archive.read_bytes()),
            '--archive-record', str(self.root / 'package.json'), '--status', str(self.root / 'status.json'),
            '--child-record', str(self.root / 'child.pid'), '--log', str(self.root / 'metadata.log'),
            '--template', str(self.root / 'template.json'), '--run-id', self.run_id,
            '--destination', str(self.root / 'process_failed'), '--report', str(self.root / 'host_report.json')])
        self.assertEqual(1, rc)
        self.assertFalse(json.loads((self.root / 'host_report.json').read_text())['batch_pass'])


if __name__ == '__main__':
    unittest.main()
