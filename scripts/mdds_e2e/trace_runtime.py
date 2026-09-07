"""Freeze installed tracing inputs and verify their board runtime bytes."""
import hashlib
import json
from pathlib import Path
import sys

PACKAGES = ('ros2trace', 'tracetools_trace', 'lttngpy')
NATIVE = ('liblttng', 'liburcu', 'libtracetools', 'libpopt', 'libxml2')


def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def pack(prefix, output):
    paths = {prefix / 'bin/lttng', prefix / 'bin/lttng-sessiond', prefix / 'Lib/lttng/libexec/lttng-consumerd'}
    paths.update(p for p in (prefix / 'Lib').iterdir() if p.name.startswith(NATIVE) and '.so' in p.name)
    for package in PACKAGES:
        paths.update(p for p in (prefix / 'Lib/site-packages' / package).rglob('*')
                     if p.is_file() and (p.suffix == '.py' or p.name.endswith('-linux-ohos.so')))
    value = {'schema_version': 1, 'files': {p.relative_to(prefix).as_posix(): sha(p) for p in sorted(paths)}}
    output.write_text(json.dumps(value, indent=2) + '\n')


def verify(root):
    path = root / 'trace_runtime.json'
    value = json.loads(path.read_bytes())
    prefix = Path('/data/local/tmp/ros2')
    for relative, expected in value['files'].items():
        p = prefix / relative
        if not p.resolve().is_relative_to(prefix.resolve()) or sha(p) != expected:
            raise ValueError('tracing runtime differs: ' + relative)
    return {'manifest_sha256': sha(path), 'files_verified': len(value['files'])}


def mapped(pid, root):
    manifest = json.loads((root / 'trace_runtime.json').read_bytes())['files']
    prefix = Path('/data/local/tmp/ros2')
    found = {}
    for line in Path(f'/proc/{pid}/maps').read_text().splitlines():
        parts = line.split(None, 5)
        if len(parts) != 6: continue
        path = Path(parts[5])
        if not path.name.startswith(NATIVE) and '_lttngpy' not in path.name: continue
        relative = path.relative_to(prefix).as_posix()
        actual = sha(path)
        if manifest.get(relative) != actual: raise ValueError('unfrozen tracing mapping: ' + str(path))
        found[relative] = actual
    return found


if __name__ == '__main__': pack(Path(sys.argv[1]), Path(sys.argv[2]))
