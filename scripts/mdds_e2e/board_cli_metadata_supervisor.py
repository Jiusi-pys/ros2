#!/usr/bin/env python3
"""Wait for the metadata runner, then publish a bounded run-owned archive."""

import sys
sys.dont_write_bytecode = True

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import tarfile

from board_graph_ownership import supervise_command


def publish_json(path, value):
    temporary = path.with_name(path.name + '.pending')
    with temporary.open('x', encoding='utf-8') as output:
        json.dump(value, output, sort_keys=True)
        output.write('\n')
        output.flush()
        os.fsync(output.fileno())
    if path.exists() or path.is_symlink():
        raise FileExistsError(path)
    temporary.rename(path)


def package_evidence(run_root):
    source = run_root / 'cli_metadata'
    if not source.is_dir() or source.is_symlink():
        raise ValueError('metadata runner did not create its output directory')
    files = sorted(path for path in source.iterdir() if path.suffix in ('.json', '.log'))
    if not 1 <= len(files) <= 128:
        raise ValueError('metadata archive member count outside 1..128')
    total = 0
    for path in files:
        if (path.is_symlink() or not path.is_file() or
                not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]*\.(?:json|log)', path.name) or
                not 0 <= path.stat().st_size <= 4 * 1024 * 1024):
            raise ValueError(f'invalid metadata evidence file: {path.name}')
        total += path.stat().st_size
    if total > 16 * 1024 * 1024:
        raise ValueError('metadata expanded archive exceeds 16 MiB')
    archive = run_root / 'cli_metadata.tar'
    temporary = run_root / 'cli_metadata.tar.pending'
    with temporary.open('xb') as output:
        with tarfile.open(fileobj=output, mode='w', format=tarfile.USTAR_FORMAT) as tar:
            for path in files:
                tar.add(path, arcname=path.name, recursive=False)
        output.flush()
        os.fsync(output.fileno())
    if temporary.stat().st_size > 16 * 1024 * 1024:
        raise ValueError('metadata archive exceeds 16 MiB including headers')
    if archive.exists() or archive.is_symlink():
        raise FileExistsError(archive)
    temporary.rename(archive)
    with archive.open('rb') as source_file:
        digest = hashlib.file_digest(source_file, 'sha256').hexdigest()
    return {'sha256': digest, 'bytes': archive.stat().st_size, 'members': len(files)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', type=Path, required=True)
    parser.add_argument('--run-id', required=True)
    parser.add_argument('--board-serial', required=True)
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]*', args.run_id):
        raise ValueError('unsafe run ID')
    run_root = args.prefix / '.mdds-owned-runs' / args.run_id
    if Path(__file__).resolve().parent != run_root.resolve():
        raise ValueError('supervisor was not staged in the selected run directory')
    argv = [sys.executable, '-B', str(run_root / 'board_cli_metadata.py'),
            '--prefix', str(args.prefix), '--run-id', args.run_id,
            '--board-serial', args.board_serial, '--output-dir', str(run_root / 'cli_metadata')]
    rc = supervise_command(argv, run_root / 'metadata.status.json', args.run_id,
                           'metadata', '/cli_metadata', run_root / 'metadata.child.pid')
    record = {'schema_version': 1, 'run_id': args.run_id, 'metadata_returncode': rc}
    try:
        record.update(package_evidence(run_root))
        record['ok'] = True
    except (ValueError, OSError, tarfile.TarError) as exc:
        record.update(ok=False, error=str(exc))
    # No later stdout is emitted. The parent can fetch the immutable log after
    # this terminal packaging record appears, including when the child failed.
    publish_json(run_root / 'metadata.archive.json', record)
    return rc if rc != 0 else 0 if record['ok'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
