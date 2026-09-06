"""Three actual cross-board CLI cases while the ROS broker fixture is held live."""
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
from board_graph_ownership import process_start
import cli_acceptance as acceptance


def oracle(case, stdout, expected):
    lines = [line.strip() for line in stdout.splitlines() if line.strip()]
    if case == 'cli:topic/type':
        return lines == [expected]
    if case == 'cli:topic/find':
        return sorted(lines) == sorted(expected)
    if case == 'cli:service/call':
        matches = re.findall(r'response:\s*example_interfaces\.srv\.AddTwoInts_Response\(sum=(-?\d+)\)', stdout)
        return matches == [str(expected)]
    return False


def execute(argv, output, run, board, case, expected):
    actual = [sys.executable, '-B', '-c', 'from ros2cli.cli import main; raise SystemExit(main())'] + argv[1:]
    with subprocess.Popen(actual, stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True) as child:
        start = process_start(child.pid)
        def stop(signum, frame):
            if child.poll() is None:
                os.killpg(child.pid, signal.SIGKILL)
            child.wait()
            raise SystemExit(128 + signum)
        previous = {s: signal.signal(s, stop) for s in (signal.SIGINT, signal.SIGTERM)}
        try:
            try:
                stdout, stderr = child.communicate(timeout=20)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL)
                stdout, stderr = child.communicate()
        finally:
            for s, handler in previous.items(): signal.signal(s, handler)
        stdout, stderr = stdout.decode('utf-8'), stderr.decode('utf-8')
        passed = child.returncode == 0 and oracle(case, stdout, expected)
        passed = passed and 'mdds transports active: dsoftbus(local=AF_UNIX physical=dsoftbus_broker' in stderr
        token = case.replace(':', '_').replace('/', '_')
        path = output / (token + '.log')
        assertion = f'MDDS_CLI_FUNCTIONAL CASE={case} RESULT=PASS'
        text = ('MDDS_CLI_ACTUAL_ARGV ' + json.dumps(actual) + '\nMDDS_CLI_STDOUT_BEGIN\n' + stdout +
                '\nMDDS_CLI_STDOUT_END\nMDDS_CLI_STDERR_BEGIN\n' + stderr + '\nMDDS_CLI_STDERR_END\n' +
                acceptance.terminal_marker(run, case, child.returncode, argv, board) + '\n')
        if passed: text += assertion + '\n'
        path.write_text(text, encoding='utf-8')
        return {'case_id': case, 'passed': passed, 'expected': expected, 'functional_marker': assertion,
                'execution': {'board_serial': board, 'argv': argv, 'actual_argv': actual,
                              'returncode': child.returncode, 'child_pid': child.pid, 'child_start': start,
                              'log': {'path': path.name, 'sha256': acceptance.digest(path.read_bytes())}}}


def main():
    root = Path(sys.argv[1]); run, board, peer, nonce = sys.argv[2:6]
    if (root / 'owner').read_text() != f'MDDS_RUN_OWNER RUN_ID={run} LABEL=ros_broker\n':
        raise ValueError('wrong run owner')
    namespace = '/ros_broker_' + run
    peer_role = 'B' if board == acceptance.TARGET['board_serials'][0] else 'A'
    service = namespace + '/' + peer_role + '/alpha/serve'
    operand = int(nonce[:7], 16) + (1 if peer_role == 'B' else 2)
    cases = [
        ('cli:topic/type', ['ros2','topic','type',namespace+'/'+peer_role+'/alpha/out','--no-daemon','--spin-time','3'], 'std_msgs/msg/String'),
        ('cli:topic/find', ['ros2','topic','find','std_msgs/msg/String','--no-daemon','--spin-time','3'],
         sorted(namespace+'/'+role+'/'+name+'/out' for role in ('A','B') for name in ('alpha','beta'))),
        ('cli:service/call', ['ros2','service','call',service,'example_interfaces/srv/AddTwoInts',json.dumps({'a':operand,'b':17})], operand+17),
    ]
    output = root / 'cli_graph'; output.mkdir(mode=0o700)
    results = [execute(argv, output, run, board, case, expected) for case, argv, expected in cases]
    report = {'run_id':run,'board':board,'peer':peer,'nonce':nonce,'results':results}
    (output/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    print('CLI_GRAPH_RESULT '+json.dumps(report),flush=True)
    return 0 if all(v['passed'] for v in results) else 1


if __name__ == '__main__': raise SystemExit(main())
