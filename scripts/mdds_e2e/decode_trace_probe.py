"""Decode hash-bound board CTF archives with WSL Babeltrace; fail closed."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess
import sys
import tarfile
from trace_contract import validate_events


def decode(root, board):
    archive_path = root / (board + '.trace_probe.tar.gz')
    destination = root / (board + '.trace_decoded')
    destination.mkdir()
    with tarfile.open(archive_path) as archive:
        members = archive.getmembers()
        if len(members) > 4096 or sum(m.size for m in members) > 256 * 1024 * 1024:
            raise ValueError('trace archive exceeds bounds')
        names = set()
        for member in members:
            path = PurePosixPath(member.name)
            if (not (member.isfile() or member.isdir()) or path.is_absolute() or '..' in path.parts
                    or not path.parts or path.parts[0] != 'trace_probe' or '\\' in member.name
                    or member.name in names):
                raise ValueError('unsafe or duplicate trace archive entry')
            names.add(member.name)
        archive.extractall(destination, filter='data')
    folder = destination / 'trace_probe'
    raw_report = (folder / 'report.json').read_bytes()
    if raw_report != (root / (board + '.trace_report.json')).read_bytes():
        raise ValueError('trace report differs from independently transferred copy')
    report = json.loads(raw_report)
    if report['run_id'] != root.name or report['nonce'] != (root / 'nonce').read_text().strip() or not report['passed']:
        raise ValueError('wrong trace run identity or failed probe')
    if [c['label'] for c in report['commands']] != ['start', 'pause', 'resume', 'stop', 'interactive']:
        raise ValueError('incomplete trace lifecycle')
    if any(c['returncode'] != 0 or c['pid'] <= 0 or not c['start'].isdecimal() for c in report['commands']):
        raise ValueError('trace command failed or lacks process identity')
    for phase, actor in report['actors'].items():
        if actor['mount_namespace'] != report['namespace'] or actor['sum'] != actor['a'] + actor['b']:
            raise ValueError('trace actor namespace or peer response differs')
        if actor['a'] != int(report['nonce'][:7], 16): raise ValueError('actor response belongs to another run')
        raw = (folder / (phase + '.actor.log')).read_text()
        if raw.splitlines().count('TRACE_ACTOR ' + json.dumps(actor)) != 1:
            raise ValueError('actor raw process evidence missing')
        if 'dsoftbus(local=AF_UNIX physical=dsoftbus_broker' not in raw:
            raise ValueError('trace actor did not select DSoftBus')
    decoded = {}
    for session, name in report['sessions'].items():
        trace_path = folder / 'traces' / name
        absolute = trace_path.resolve().as_posix()
        linux_path = '/mnt/' + absolute[0].lower() + absolute[2:]
        result = subprocess.run(['wsl.exe', '-d', 'Ubuntu-20.04', '--', 'babeltrace', linux_path],
                                capture_output=True, timeout=30)
        (destination / (session + '.decoded.log')).write_bytes(result.stdout)
        (destination / (session + '.decoder.stderr')).write_bytes(result.stderr)
        if result.returncode != 0: raise RuntimeError('Babeltrace failed')
        decoded[session] = result.stdout.decode('utf-8')
    validate_events(decoded, report['actors'])
    result = {'board': board, 'run_id': report['run_id'], 'nonce': report['nonce'],
              'archive_sha256': hashlib.sha256(archive_path.read_bytes()).hexdigest(),
              'decoded_sha256': {k: hashlib.sha256(v.encode()).hexdigest() for k, v in decoded.items()},
              'validated_phases': list(report['actors']), 'cli_acceptance_advanced': False}
    (destination / 'verification.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


if __name__ == '__main__':
    print(json.dumps(decode(Path(sys.argv[1]), sys.argv[2]), indent=2))
