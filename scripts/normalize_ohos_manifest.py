#!/usr/bin/env python3
"""Write a portable, create-only SHA-256 manifest for generic ROS deployment."""
import argparse
from pathlib import Path
import re

RESERVED = {'env.sh', 'deploy_manifest.sha256', 'release_provenance.json',
            'build_receipt.json', '.ros2_deploy_complete', '.ros2-owned-runs',
            '.ros2-activity-lock', '.ros2-generic-deploy.lock'}


def normalize(source: Path, destination: Path) -> None:
    if source.is_symlink() or not source.is_file():
        raise ValueError('manifest input must be a regular non-link file')
    records = []
    seen = set()
    for line in source.read_text(encoding='utf-8').splitlines():
        match = re.fullmatch(r'([0-9a-f]{64}) [ *](\./.+)', line)
        if match is None:
            raise ValueError('invalid checksum record')
        digest, name = match.groups()
        components = name[2:].split('/')
        if (any(part in ('', '.', '..') for part in components) or '\\' in name or
                any(ord(char) < 32 or ord(char) == 127 for char in name) or
                components[0] in RESERVED or name in seen):
            raise ValueError(f'unsafe, duplicate or reserved manifest path: {name!r}')
        seen.add(name)
        records.append(f'{digest}  {name}\n')
    if not records:
        raise ValueError('empty checksum manifest')
    # Open after validation; O_EXCL also refuses a dangling output symlink.
    with destination.open('xb') as output:
        output.write(''.join(records).encode('utf-8'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    arguments = parser.parse_args()
    normalize(arguments.source, arguments.destination)
