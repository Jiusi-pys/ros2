#!/usr/bin/env python3
"""Private rclpy package staging and same-context lifetime evidence."""
import sys
sys.dont_write_bytecode = True
import argparse
from collections import Counter
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import runpy
import stat
import tarfile

MAX_FILE = 16 * 1024 * 1024
MAX_TOTAL = 32 * 1024 * 1024
CASES = {
    'test_last_native_impl_copy_owns_service_until_explicit_release':
        [('before_copy', 1), ('copy_retains_service', 1), ('after_last_copy_release', 0)],
    'test_node_destroy_retires_type_description_service_and_both_endpoints':
        [('before_node_destroy', 1), ('after_node_destroy', 0)],
}
FIXTURE = 'test_type_description_service_lifetime.py'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def regular_bytes(path, limit=MAX_FILE):
    path = Path(path)
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode) or not 0 <= info.st_size <= limit:
        raise ValueError('not a bounded regular file: ' + str(path))
    data = path.read_bytes()
    if len(data) != info.st_size:
        raise ValueError('file changed while reading: ' + str(path))
    return data


def find_native(package):
    package = Path(package)
    if not package.is_dir() or package.is_symlink():
        raise ValueError('rclpy package must be a real directory')
    found = list(package.glob('_rclpy_pybind11*.so'))
    if len(found) != 1 or not found[0].is_file() or found[0].is_symlink():
        raise ValueError('expected exactly one actual rclpy native .so suffix')
    return found[0]


def native_elf(data):
    if (len(data) < 64 or data[:6] != b'\x7fELF\x02\x01' or
            int.from_bytes(data[18:20], 'little') != 183):
        raise ValueError('rclpy native must be an AArch64 ELF64 shared object')


def safe_member(name):
    path = PurePosixPath(name)
    return (not path.is_absolute() and len(path.parts) >= 2 and path.parts[0] == 'rclpy' and
            all(re.fullmatch(r'[A-Za-z0-9_][A-Za-z0-9_.-]*', part) for part in path.parts) and
            str(path) == name and '\\' not in name)


def pack_rclpy(package, native, archive):
    package, archive = Path(package), Path(archive)
    installed_native = find_native(package)
    native = Path(native) if native else installed_native
    if native.name != installed_native.name:
        raise ValueError('selected native basename differs from the actual installed import suffix')
    native_data = regular_bytes(native)
    native_elf(native_data)
    files = {}
    for source in sorted(package.rglob('*')):
        relative = source.relative_to(package)
        if '__pycache__' in relative.parts or source.suffix in ('.pyc', '.pyo'):
            continue
        if source.is_symlink():
            raise ValueError('rclpy package links are not accepted')
        if source.is_dir():
            continue
        name = 'rclpy/' + relative.as_posix()
        if not safe_member(name):
            raise ValueError('unsafe package member name')
        files[name] = native_data if source == installed_native else regular_bytes(source)
    if 'rclpy/__init__.py' not in files or 'rclpy/' + native.name not in files:
        raise ValueError('incomplete rclpy package')
    if not 1 <= len(files) <= 256 or sum(map(len, files.values())) > MAX_TOTAL:
        raise ValueError('package exceeds bounded member/byte capacity')
    with archive.open('xb') as output:
        with tarfile.open(fileobj=output, mode='w', format=tarfile.USTAR_FORMAT) as tar:
            for name, data in files.items():
                info = tarfile.TarInfo(name)
                info.size, info.mode, info.mtime = len(data), 0o600, 0
                tar.addfile(info, io.BytesIO(data))
    return {'schema_version': 1, 'native_name': native.name, 'native_sha256': digest(native_data),
            'archive_sha256': digest(regular_bytes(archive, MAX_TOTAL + 2 * 1024 * 1024)),
            'files': {name: {'bytes': len(data), 'sha256': digest(data)} for name, data in files.items()}}


def extract_rclpy(archive, manifest, destination):
    destination = Path(destination)
    raw = regular_bytes(archive, MAX_TOTAL + 2 * 1024 * 1024)
    if not isinstance(manifest, dict) or digest(raw) != manifest.get('archive_sha256'):
        raise ValueError('package archive hash mismatch')
    expected = manifest.get('files')
    if not isinstance(expected, dict) or not 1 <= len(expected) <= 256:
        raise ValueError('invalid package member inventory')
    files, total = {}, 0
    with tarfile.open(fileobj=io.BytesIO(raw), mode='r:') as tar:
        for member in tar.getmembers():
            if (not member.isfile() or not safe_member(member.name) or
                    member.name in files or not 0 <= member.size <= MAX_FILE):
                raise ValueError('unsafe, linked or duplicate package member')
            total += member.size
            if total > MAX_TOTAL or len(files) >= 256:
                raise ValueError('package expanded capacity exceeded')
            data = tar.extractfile(member).read(MAX_FILE + 1)
            if expected.get(member.name) != {'bytes': len(data), 'sha256': digest(data)}:
                raise ValueError('package member hash or length mismatch')
            files[member.name] = data
    if set(files) != set(expected) or 'rclpy/__init__.py' not in files:
        raise ValueError('incomplete package member set')
    native_name = manifest.get('native_name', '')
    if (not re.fullmatch(r'_rclpy_pybind11[A-Za-z0-9_.-]*\.so', native_name) or
            'rclpy/' + native_name not in files or
            digest(files['rclpy/' + native_name]) != manifest.get('native_sha256')):
        raise ValueError('native extension missing from package')
    native_elf(files['rclpy/' + native_name])
    if destination.exists() or destination.is_symlink():
        raise ValueError('overlay destination already exists')
    pending = destination.with_name(destination.name + '.pending')
    pending.mkdir(mode=0o700)
    for name, data in files.items():
        target = pending / name
        target.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        with target.open('xb') as output:
            output.write(data)
    if destination.exists() or destination.is_symlink():
        raise ValueError('overlay destination appeared before publication')
    pending.rename(destination)
    return destination / 'rclpy' / native_name


def validate_result(log, status, record, plan, root):
    errors, functional = [], []
    run_id = plan.get('run_id')
    result = None
    def records(prefix):
        values = []
        for line in log.splitlines():
            if prefix not in line:
                continue
            try:
                value = json.loads(line.split(prefix, 1)[1])
                if not isinstance(value, dict):
                    raise ValueError('not object')
                values.append(value)
            except (ValueError, TypeError):
                errors.append('malformed ' + prefix.strip())
        return values
    def one(prefix):
        values = records(prefix)
        if len(values) != 1:
            errors.append('missing/duplicate ' + prefix.strip())
            return {}
        return values[0]
    if not isinstance(status, dict):
        status = {}
    for key, value in {'schema_version': 1, 'run_id': run_id, 'role': 'type_lifetime',
                       'namespace': '/type_lifetime_' + str(run_id)}.items():
        if status.get(key) != value:
            errors.append('wrong real-wait ' + key)
    pid, start, rc = status.get('child_pid'), status.get('child_start'), status.get('returncode')
    if type(pid) is not int or pid <= 0 or not isinstance(start, str) or not re.fullmatch(r'[1-9][0-9]*', start):
        errors.append('invalid real child PID/start')
    if type(rc) is not int:
        errors.append('missing actual child return code')
    if record.strip() != f'MDDS_OWNED_PROCESS RUN_ID={run_id} TAG=type_lifetime_child PID={pid} START={start}':
        errors.append('owned child record mismatch')
    if one('GRAPH_PROCESS_EXIT ') != status:
        errors.append('log/terminal real-wait mismatch')
    result = one('TYPE_DESCRIPTION_RUN_RESULT ')
    for key, value in {'schema_version': 1, 'run_id': run_id, 'pid': pid,
                       'fixture_exit': rc, 'fixture_sha256': plan.get('fixture_sha256'),
                       'scope': 'same-context-node-lifetime', 'cross_board_proven': False}.items():
        if result.get(key) != value or (key == 'fixture_exit' and type(result.get(key)) is not int):
            errors.append('wrong run result ' + key)
    native = plan.get('package', {}).get('native_name', '')
    expected_files = {name: {'path': root + '/lib/' + name, 'sha256': sha}
                      for name, sha in plan.get('libraries', {}).items()}
    expected_files[native] = {'path': root + '/python/rclpy/' + native,
                             'sha256': plan.get('package', {}).get('native_sha256')}
    for phase in ('before', 'after'):
        value = result.get(phase, {})
        expected = {'pid': pid, 'rmw': 'rmw_mdds', 'files': expected_files,
                    'rclpy_package': root + '/python/rclpy/__init__.py',
                    'profile': 'ohos_dsoftbus', 'domain': str(plan.get('domain')),
                    'discovery_range': 'SYSTEM_DEFAULT', 'legacy_transport': None, 'broker_socket': None}
        if not isinstance(value, dict) or any(value.get(k) != v for k, v in expected.items()):
            errors.append('wrong loaded native/library/profile provenance at ' + phase)
    transports = [line.split('mdds transports active: ', 1)[1] for line in log.splitlines()
                  if 'mdds transports active: ' in line]
    if len(transports) != 2 or any(re.fullmatch(r'dsoftbus\([^)]*\)', t) is None for t in transports):
        errors.append('expected two successful production DSoftBus Context starts')
    summaries = re.findall(r'(?m)^Ran ([0-9]+) tests? in [0-9.]+s\s*$', log)
    if summaries != ['2']:
        errors.append('missing exact two-test unittest execution summary')
    observations = records('TYPE_DESCRIPTION_LIFETIME_OBSERVED ')
    expected_pairs = {(case, phase): count for case, phases in CASES.items() for phase, count in phases}
    pairs = [(o.get('case'), o.get('phase')) for o in observations]
    if Counter(pairs) != Counter({pair: 1 for pair in expected_pairs}):
        errors.append('missing/duplicate lifecycle case phase')
    for observed in observations:
        pair = (observed.get('case'), observed.get('phase'))
        if pair not in expected_pairs:
            continue
        count = expected_pairs[pair]
        expected = {'expected_count': count, 'matched': True, 'context_ok': True,
                    'observer_service_count': 1, 'service_count': count,
                    'request_readers': count, 'response_writers': count,
                    'service_types': ['type_description_interfaces/srv/GetTypeDescription'] if count else [],
                    'node_names': ['alpha', 'observer'] if count else ['observer']}
        if observed.get('service_name') != f'/rclpy_type_lifetime_{pid}/{pair[0]}/alpha/get_type_description':
            errors.append('lifecycle phase belongs to another process/namespace')
        strict_types = (all(type(observed.get(k)) is int for k in ('expected_count', 'observer_service_count', 'service_count', 'request_readers', 'response_writers')) and all(type(observed.get(k)) is bool for k in ('matched', 'context_ok')))
        if not strict_types or any(observed.get(k) != v for k, v in expected.items()):
            functional.append('graph lifetime mismatch: ' + '/'.join(pair))
        for field in ('request_gids', 'response_gids'):
            gids = observed.get(field)
            if (not isinstance(gids, list) or len(gids) != count or
                    any(not isinstance(gid, list) or not gid or not any(gid) or
                        any(type(x) is not int or not 0 <= x <= 255 for x in gid) for gid in gids)):
                functional.append('invalid endpoint GIDs: ' + '/'.join(pair))
    if rc != 0:
        functional.append('actual unittest child returned nonzero')
    if len(re.findall(r'(?m)^OK\s*$', log)) != 1:
        functional.append('unittest did not finish with exact OK (no skips)')
    return {'run_id': run_id, 'valid_evidence': not errors, 'passed': not errors and not functional,
            'errors': errors, 'functional_errors': functional, 'fixture_exit': rc,
            'cross_board_proven': False, 'scope': 'same-context-node-lifetime'}


def publish_json(path, value):
    path = Path(path)
    pending = path.with_name(path.name + '.pending')
    with pending.open('x', encoding='utf-8') as output:
        json.dump(value, output, sort_keys=True)
        output.write('\n')
        output.flush()
        os.fsync(output.fileno())
    if path.exists() or path.is_symlink():
        raise FileExistsError(path)
    pending.rename(path)


def owned_root(value):
    root = Path(value)
    if (root.is_symlink() or root.resolve() != Path(__file__).resolve().parent or
            root.parent.name != '.mdds-owned-runs' or
            not re.fullmatch(r'[A-Za-z0-9_][A-Za-z0-9_.-]{0,95}', root.name)):
        raise ValueError('runner is not in the selected run-owned directory')
    expected = f'MDDS_RUN_OWNER RUN_ID={root.name} LABEL=type_lifetime\n'.encode()
    if regular_bytes(root / 'owner', 1024) != expected:
        raise ValueError('wrong run ownership marker')
    return root


def load_plan(root):
    plan = json.loads(regular_bytes(root / 'lifetime.plan.json', 256 * 1024))
    if (plan.get('schema_version') != 1 or plan.get('run_id') != root.name or
            type(plan.get('domain')) is not int or not 1 <= plan['domain'] <= 232):
        raise ValueError('invalid run plan identity/domain')
    checks = {FIXTURE: plan.get('fixture_sha256'), 'profile.env': plan.get('profile_sha256'),
              'mdds_token_exec': plan.get('token_sha256'),
              'rclpy_overlay.tar': plan['package'].get('archive_sha256')}
    checks.update({'lib/' + name: sha for name, sha in plan['libraries'].items()})
    if set(plan['libraries']) != {'libmdds.so', 'librmw_mdds.so'}:
        raise ValueError('incomplete two-library overlay plan')
    for name, expected in checks.items():
        if not isinstance(expected, str) or not re.fullmatch(r'[0-9a-f]{64}', expected):
            raise ValueError('invalid frozen artifact digest: ' + name)
        limit = MAX_TOTAL + 2 * 1024 * 1024 if name.endswith('.tar') else MAX_FILE
        if digest(regular_bytes(root / name, limit)) != expected:
            raise ValueError('run input changed: ' + name)
    return plan


def provenance(root, plan):
    import rclpy
    from rclpy.impl.implementation_singleton import rclpy_implementation
    from rclpy.utilities import get_rmw_implementation_identifier
    rmw = get_rmw_implementation_identifier()
    native = plan['package']['native_name']
    expected = {name: str(root / 'lib' / name) for name in plan['libraries']}
    expected[native] = str(root / 'python/rclpy' / native)
    found = {}
    for line in Path('/proc/self/maps').read_text().splitlines():
        fields = line.split(None, 5)
        if len(fields) != 6:
            continue
        path = fields[5].strip()
        name = Path(path.removesuffix(' (deleted)')).name
        if name in plan['libraries'] or name.startswith('_rclpy_pybind11'):
            found.setdefault(name, set()).add(path)
    if found != {name: {path} for name, path in expected.items()}:
        raise ValueError(f'wrong actual mapped rclpy/RMW/MDDS overlay: {found}')
    # Every Python source came from the bounded package archive, and must
    # retain those exact bytes before/after running the standalone fixture.
    for relative, info in plan['package']['files'].items():
        content = regular_bytes(root / 'python' / relative)
        if len(content) != info['bytes'] or digest(content) != info['sha256']:
            raise ValueError('extracted rclpy package changed: ' + relative)
    if (rclpy_implementation.__file__ != expected[native] or
            rclpy.__file__ != str(root / 'python/rclpy/__init__.py')):
        raise ValueError('rclpy package or native import bypassed the private package')
    hashes = dict(plan['libraries'], **{native: plan['package']['native_sha256']})
    files = {}
    for name, path in expected.items():
        actual = digest(regular_bytes(path))
        if actual != hashes[name]:
            raise ValueError('mapped artifact hash changed: ' + name)
        files[name] = {'path': path, 'sha256': actual}
    return {'pid': os.getpid(), 'rmw': rmw, 'files': files, 'rclpy_package': rclpy.__file__,
            'profile': os.environ.get('MDDS_DEPLOYMENT_PROFILE'),
            'domain': os.environ.get('ROS_DOMAIN_ID'),
            'discovery_range': os.environ.get('ROS_AUTOMATIC_DISCOVERY_RANGE'),
            'legacy_transport': os.environ.get('MDDS_TRANSPORT'),
            'broker_socket': os.environ.get('MDDS_BROKER_LOCAL_TEST_SOCKET'),
            'process_argv': Path('/proc/self/cmdline').read_bytes().rstrip(b'\0').decode().split('\0')}


def run_fixture(root):
    plan = load_plan(root)
    before = provenance(root, plan)
    expected_profile = {'rmw': 'rmw_mdds', 'profile': 'ohos_dsoftbus',
                        'domain': str(plan['domain']), 'discovery_range': 'SYSTEM_DEFAULT',
                        'legacy_transport': None, 'broker_socket': None}
    if any(before.get(k) != v for k, v in expected_profile.items()):
        raise ValueError('production DSoftBus profile was not applied')
    fixture = root / FIXTURE
    previous = sys.argv
    sys.argv = [str(fixture), '-v']
    try:
        try:
            runpy.run_path(str(fixture), run_name='__main__')
            rc = 0
        except SystemExit as exc:
            if not isinstance(exc.code, (int, type(None))):
                raise ValueError('unittest produced a non-numeric process exit') from exc
            rc = int(exc.code or 0)
    finally:
        sys.argv = previous
    after = provenance(root, plan)
    load_plan(root)
    print('TYPE_DESCRIPTION_RUN_RESULT ' + json.dumps({
        'schema_version': 1, 'run_id': root.name, 'pid': os.getpid(), 'fixture_exit': rc,
        'fixture_sha256': plan['fixture_sha256'], 'fixture_argv': [str(fixture), '-v'],
        'scope': 'same-context-node-lifetime', 'cross_board_proven': False,
        'before': before, 'after': after}, sort_keys=True), flush=True)
    return rc


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=('pack', 'plan', 'prepare', 'supervise', 'run', 'verify'))
    parser.add_argument('--package', type=Path)
    parser.add_argument('--native', type=Path)
    parser.add_argument('--archive', type=Path)
    parser.add_argument('--manifest', type=Path)
    parser.add_argument('--inputs', type=Path)
    parser.add_argument('--run-id')
    parser.add_argument('--domain', type=int, default=51)
    parser.add_argument('--run-root')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.operation == 'pack':
        if not all((args.package, args.archive, args.manifest)):
            parser.error('pack needs package/archive/manifest')
        publish_json(args.manifest, pack_rclpy(args.package, args.native, args.archive))
        return 0
    if args.operation == 'plan':
        if (not args.inputs or not args.output or not args.run_id or
                not re.fullmatch(r'[A-Za-z0-9_][A-Za-z0-9_.-]{0,95}', args.run_id) or
                not 1 <= args.domain <= 232):
            parser.error('invalid plan inputs/run/domain')
        package = json.loads(regular_bytes(args.inputs / 'rclpy_package.json', 256 * 1024))
        plan = {'schema_version': 1, 'run_id': args.run_id, 'domain': args.domain, 'package': package,
                'libraries': {name: digest(regular_bytes(args.inputs / name))
                              for name in ('libmdds.so', 'librmw_mdds.so')},
                'fixture_sha256': digest(regular_bytes(args.inputs / FIXTURE)),
                'profile_sha256': digest(regular_bytes(args.inputs / 'profile.env')),
                'token_sha256': digest(regular_bytes(args.inputs / 'mdds_token_exec'))}
        publish_json(args.output, plan)
        return 0
    if not args.run_root:
        parser.error('operation requires run-root')
    if args.operation == 'verify':
        if not args.inputs or not args.output:
            parser.error('verify requires inputs/output')
        plan = json.loads(regular_bytes(args.inputs / 'lifetime.plan.json', 256 * 1024))
        report = validate_result(regular_bytes(args.inputs / 'lifetime.log').decode('utf-8'),
            json.loads(regular_bytes(args.inputs / 'lifetime.status.json')),
            regular_bytes(args.inputs / 'lifetime.child.pid').decode(), plan, args.run_root)
        report['log_sha256'] = digest(regular_bytes(args.inputs / 'lifetime.log'))
        publish_json(args.output, report)
        print('TYPE_DESCRIPTION_LIFETIME_CASE ' + json.dumps(report, sort_keys=True), flush=True)
        return 0 if report['passed'] else 1
    root = owned_root(args.run_root)
    if args.operation == 'prepare':
        plan = load_plan(root)
        native = extract_rclpy(root / 'rclpy_overlay.tar', plan['package'], root / 'python')
        from broker_local_run import mark_executable
        mark_executable(root / 'mdds_token_exec', plan['token_sha256'])
        print('TYPE_DESCRIPTION_OVERLAY_READY native=' + native.name, flush=True)
        return 0
    if args.operation == 'supervise':
        load_plan(root)
        from board_graph_ownership import supervise_command
        # The token launcher execs Python in the same child PID. Its actual
        # interpreter exit (including teardown) remains the supervisor result.
        argv = [str(root / 'mdds_token_exec'), '--', sys.executable, '-B', str(Path(__file__)),
                'run', '--run-root', str(root)]
        return supervise_command(argv, root / 'lifetime.status.json', root.name,
            'type_lifetime', '/type_lifetime_' + root.name, root / 'lifetime.child.pid')
    return run_fixture(root)


if __name__ == '__main__':
    raise SystemExit(main())
