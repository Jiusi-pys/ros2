"""Bind actual tracing commands, runtime, peer callbacks and decoded CTF."""
import json
import tarfile
import cli_acceptance as a
from trace_contract import validate_report, validate_events, recipe, PHASES


def validate(value, root, run, board, nonce):
    role = 'A' if board == a.TARGET['board_serials'][0] else 'B'
    report = value['trace_probe']
    manifest_path = root / 'trace_runtime.json'
    manifest = json.loads(manifest_path.read_bytes())
    validate_report(report, run, nonce, role, a.digest(manifest_path.read_bytes()))
    if report['runtime_before']['files_verified'] != len(manifest['files']): raise ValueError('runtime inventory count differs')
    raw_report = (root / (board + '.trace_report.json')).read_bytes()
    if json.loads(raw_report) != report: raise ValueError('tracing report differs from batch')
    folder = root / (board + '.trace_decoded')
    proof = json.loads((folder / 'verification.json').read_bytes())
    archive = root / (board + '.trace_probe.tar.gz')
    if proof['archive_sha256'] != a.digest(archive.read_bytes()) or proof['board'] != board or proof['run_id'] != run or proof['nonce'] != nonce:
        raise ValueError('decoded trace proof refers to another archive/run')
    with tarfile.open(archive) as tar:
        files = {m.name: tar.extractfile(m).read() for m in tar.getmembers() if m.isfile()}
    if files.get('trace_probe/report.json') != raw_report: raise ValueError('archived report differs')
    decoded = {}
    for session in ('lifecycle', 'interactive'):
        raw = (folder / (session + '.decoded.log')).read_bytes()
        if a.digest(raw) != proof['decoded_sha256'][session]: raise ValueError('decoded trace bytes changed')
        decoded[session] = raw.decode()
    validate_events(decoded, report['actors'])
    for phase, actor in report['actors'].items():
        for path, sha in actor['trace_mappings'].items():
            if manifest['files'].get(path) != sha: raise ValueError('actor tracing library differs')
        raw = files['trace_probe/' + phase + '.actor.log'].decode()
        if raw.splitlines().count('TRACE_ACTOR ' + json.dumps(actor)) != 1 or 'dsoftbus(local=AF_UNIX physical=dsoftbus_broker' not in raw:
            raise ValueError('actor provenance/payload not bound to raw log')
    peer_board = next(b for b in a.TARGET['board_serials'] if b != board)
    expected = {'run_id': run, 'nonce': nonce, 'board': peer_board, 'peer_role': role,
                'received': ['|'.join((run, nonce, role, phase)) for phase in PHASES]}
    if json.loads((root / (peer_board + '.trace_received.json')).read_bytes()) != expected:
        raise ValueError('peer trace publication callback differs')
    if (root / (peer_board + '.ros.log')).read_text().splitlines().count('TRACE_PEER_RX ' + json.dumps(expected)) != 1:
        raise ValueError('peer trace callback missing from native run')
    results = [r for r in value['results'] if r['case_id'].startswith('cli:trace')]
    if results != report['results'] or len(results) != 5: raise ValueError('tracing executions differ from worker')
    for command, result, row in zip(report['commands'], results, recipe(run, nonce)):
        case, label, args, expected = row
        execution = result['execution']
        identity = {'argv': ['ros2'] + args, 'actual_argv': command['argv'], 'child_pid': command['pid'],
                    'child_start': command['start'], 'returncode': 0, 'board_serial': board}
        if any(execution.get(k) != v for k, v in identity.items()): raise ValueError('tracing execution identity differs')
        raw = a.read_artifact({**execution['log'], 'path': board + '.' + execution['log']['path']}, root).decode()
        label = command['label']
        for stream in ('stdout', 'stderr'):
            text = files['trace_probe/' + label + '.' + stream].decode()
            begin, end = 'MDDS_CLI_' + stream.upper() + '_BEGIN\n', '\nMDDS_CLI_' + stream.upper() + '_END'
            if raw.count(begin) != 1 or raw.count(end) != 1 or raw.split(begin)[1].split(end)[0] != text:
                raise ValueError('trace command output differs from archive')
        marker = a.terminal_marker(run, case, 0, ['ros2'] + args, board)
        if raw.splitlines().count(marker) != 1: raise ValueError('tracing terminal marker missing')
    interactive = files['trace_probe/interactive.stdout'].decode()
    if any(interactive.count(prompt) != 1 for prompt in ('press enter to start...', 'press enter to stop...', 'stopping & destroying tracing session')):
        raise ValueError('interactive tracing did not complete both prompt transitions')
