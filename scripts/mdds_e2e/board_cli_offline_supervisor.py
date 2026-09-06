#!/usr/bin/env python3
"""Use the existing real-wait supervisor and bounded public JSON/log archive."""
import sys
sys.dont_write_bytecode = True
import argparse
from pathlib import Path
from board_graph_ownership import supervise_command
from board_cli_metadata_supervisor import package_evidence, publish_json


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--prefix', type=Path, required=True)
    p.add_argument('--run-id', required=True)
    p.add_argument('--board-serial', required=True)
    args = p.parse_args()
    root = args.prefix / '.mdds-owned-runs' / args.run_id
    if Path(__file__).resolve().parent != root.resolve(): raise ValueError('wrong run directory')
    argv = [sys.executable, '-B', str(root / 'board_cli_offline.py'), '--prefix', str(args.prefix),
            '--run-id', args.run_id, '--board-serial', args.board_serial,
            '--output-dir', str(root / 'cli_metadata')]
    rc = supervise_command(argv, root / 'metadata.status.json', args.run_id, 'metadata',
                           '/cli_metadata', root / 'metadata.child.pid')
    record = {'schema_version': 1, 'run_id': args.run_id, 'metadata_returncode': rc}
    try:
        record.update(package_evidence(root)); record['ok'] = True
    except Exception as exc:
        record.update(ok=False, error=str(exc))
    publish_json(root / 'metadata.archive.json', record)
    return rc if rc else 0 if record['ok'] else 1

if __name__ == '__main__': raise SystemExit(main())
