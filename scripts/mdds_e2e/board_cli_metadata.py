#!/usr/bin/env python3
"""Run twelve real, read-only ROS 2 metadata CLI cases with exact oracles.

The parent owns board setup and sources the OHOS DSoftBus profile. This script
never invokes HDC, starts ROS nodes or changes the 98-case acceptance standard.
"""

import sys

# Do this before importing sibling/installed modules: even bytecode caches
# must not be written beside the staged runner or into the deployed prefix.
sys.dont_write_bytecode = True

import argparse
import configparser
import copy
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import xml.etree.ElementTree as ET

import cli_acceptance as acceptance


CASE_COMMANDS = {
    'cli:pkg/list': [['ros2', 'pkg', 'list']],
    'cli:pkg/prefix': [['ros2', 'pkg', 'prefix', 'rmw_mdds']],
    'cli:pkg/xml': [['ros2', 'pkg', 'xml', 'rmw_mdds']],
    'cli:pkg/executables': [
        ['ros2', 'pkg', 'executables', package, '--full-path']
        for package in ('demo_nodes_cpp', 'demo_nodes_py')],
    'cli:interface/list': [['ros2', 'interface', 'list']],
    'cli:interface/package': [['ros2', 'interface', 'package', 'example_interfaces']],
    'cli:interface/packages': [['ros2', 'interface', 'packages']],
    'cli:interface/show': [
        ['ros2', 'interface', 'show', '--no-comments', name]
        for name in ('std_msgs/msg/String', 'example_interfaces/srv/AddTwoInts',
                     'action_tutorials_interfaces/action/Fibonacci')],
    'cli:interface/proto': [['ros2', 'interface', 'proto', 'std_msgs/msg/String', '--no-quotes']],
    'cli:extension_points': [['ros2', 'extension_points', '--all']],
    'cli:extensions': [['ros2', 'extensions', '--all']],
    'cli:plugin/list': [['ros2', 'plugin', 'list', '--package', 'rosbag2_storage_sqlite3']],
}

SHOW_FIELDS = [
    ['string data'],
    ['int64 a', 'int64 b', '---', 'int64 sum'],
    ['int32 order', '---', 'int32[] sequence', '---', 'int32[] partial_sequence'],
]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def extension_registrations(contents):
    configurations = []
    points = {}
    for content in contents:
        config = configparser.ConfigParser(interpolation=None)
        config.optionxform = str
        config.read_string(content)
        configurations.append(config)
        if config.has_section('ros2cli.extension_point'):
            for name, value in config.items('ros2cli.extension_point'):
                if name in points:
                    raise ValueError(f'duplicate registered extension point: {name}')
                points[name] = value
    groups = {}
    for config in configurations:
        for section in points:
            if not config.has_section(section):
                continue
            entries = groups.setdefault(section, {})
            for name, value in config.items(section):
                if name in entries:
                    raise ValueError(f'duplicate installed CLI entry point: {section}/{name}')
                entries[name] = value
    return sorted(points), {group: sorted(entries) for group, entries in sorted(groups.items())}


def collect_context(prefix):
    """Read expected data independently from ament/XML/distribution files."""
    prefix = Path(prefix).resolve()
    inputs = {}

    def read(path):
        path = Path(path)
        if not path.resolve().is_relative_to(prefix) or not path.is_file():
            raise ValueError(f'missing or out-of-prefix metadata artifact: {path}')
        data = path.read_bytes()
        inputs[path.relative_to(prefix).as_posix()] = sha(data)
        return data.decode('utf-8')

    index = prefix / 'share/ament_index/resource_index'
    packages = sorted(path.name for path in (index / 'packages').iterdir() if path.is_file())
    for package in packages:
        read(index / 'packages' / package)
    required = {'rmw_mdds', 'demo_nodes_cpp', 'demo_nodes_py', 'std_msgs',
                'example_interfaces', 'action_tutorials_interfaces', 'rosbag2_storage_sqlite3'}
    if not required <= set(packages):
        raise ValueError('missing fixture packages: ' + ', '.join(sorted(required - set(packages))))
    package_xml = read(prefix / 'share/rmw_mdds/package.xml')
    interfaces = {name: set() for name in ('Messages', 'Services', 'Actions')}
    package_interfaces = set()
    interface_packages = []
    sections = {'msg': 'Messages', 'srv': 'Services', 'action': 'Actions'}
    for marker in sorted((index / 'rosidl_interfaces').iterdir()):
        if not marker.is_file():
            continue
        interface_packages.append(marker.name)
        for relative in read(marker).splitlines():
            # rosidl_runtime_py filters generated hidden interfaces and merges
            # the .idl and source-interface registrations into one public name.
            if '_' in relative:
                continue
            path = Path(relative)
            if path.suffix not in ('.idl', '.msg', '.srv', '.action'):
                continue
            category = path.parts[0] if path.parts else ''
            if category not in sections:
                continue
            name = marker.name + '/' + path.with_suffix('').as_posix()
            interfaces[sections[category]].add(name)
            if marker.name == 'example_interfaces':
                package_interfaces.add(name)
    # Show/proto fixtures are deliberately simple and independently pinned to
    # their installed source definitions, including all msg/srv/action fields.
    for interface, expected in zip(
            ('std_msgs/msg/String.msg', 'example_interfaces/srv/AddTwoInts.srv',
             'action_tutorials_interfaces/action/Fibonacci.action'), SHOW_FIELDS):
        actual = [line.split('#', 1)[0].strip() for line in read(prefix / 'share' / interface).splitlines()]
        if [line for line in actual if line] != expected:
            raise ValueError(f'installed interface fixture changed: {interface}')
    executables = {}
    for package in ('demo_nodes_cpp', 'demo_nodes_py'):
        paths = []
        for root, directories, files in os.walk(prefix / 'lib' / package):
            directories[:] = sorted(name for name in directories if not name.startswith('.'))
            for name in sorted(files):
                path = Path(root) / name
                if os.access(path, os.X_OK):
                    paths.append(path.as_posix())
        if not paths or not any(Path(path).name in ('talker', 'talker-script.py') for path in paths):
            raise ValueError(f'missing executable talker fixture for {package}')
        executables[package] = sorted(paths)

    metadata_roots = [prefix / 'Lib/site-packages']
    metadata_roots.extend(sorted((prefix / 'lib').glob('python*/site-packages')))
    metadata_files = sorted({path for root in metadata_roots for suffix in ('*.egg-info', '*.dist-info')
                             for path in root.glob(suffix + '/entry_points.txt')})
    extension_points, extensions = extension_registrations([read(path) for path in metadata_files])
    if not extension_points or 'ros2cli.command' not in extensions:
        raise ValueError('missing ROS 2 CLI extension registrations')

    plugin_package = 'rosbag2_storage_sqlite3'
    plugin_lines = []
    for resource_type in sorted(index.iterdir()):
        if '__pluginlib__plugin' not in resource_type.name:
            continue
        marker = resource_type / plugin_package
        if not marker.is_file():
            continue
        for relative in read(marker).splitlines():
            tree = ET.fromstring(read(prefix / relative.split(';', 1)[0]))
            libraries = [tree] if tree.tag == 'library' else list(tree.iter('library'))
            for library in libraries:
                library_name = library.attrib['path']
                library_path = prefix / 'lib' / ('lib' + library_name + '.so')
                if not library_path.is_file() or not library_path.resolve().is_relative_to(prefix):
                    raise ValueError(f'missing installed plugin library: {library_path}')
                inputs[library_path.relative_to(prefix).as_posix()] = sha(library_path.read_bytes())
            for element in tree.iter('class'):
                plugin_lines.append(f'{element.attrib.get("name", element.attrib["type"])} '
                                    f'[{element.attrib["type"]}] (base: {element.attrib["base_class_type"]})')
    if not plugin_lines:
        raise ValueError('no installed sqlite3 plugin classes')
    return {
        'prefix': prefix.as_posix(), 'packages': packages, 'package_xml': package_xml,
        'interfaces': {key: sorted(value) for key, value in interfaces.items()},
        'package_interfaces': sorted(package_interfaces), 'interface_packages': sorted(interface_packages),
        'executables': executables, 'extension_points': extension_points, 'extensions': extensions,
        'plugin_package': plugin_package, 'plugin_lines': plugin_lines,
        'input_hashes': [{'path': key, 'sha256': value} for key, value in sorted(inputs.items())],
    }


def nonempty_lines(text):
    return [line.strip() for line in text.splitlines() if line.strip()]


def exact_set(actual, expected):
    return len(actual) == len(set(actual)) and set(actual) == set(expected)


def xml_structure(element):
    return (element.tag, tuple(sorted(element.attrib.items())), (element.text or '').strip(),
            tuple(xml_structure(child) for child in element))


def check_case(case_id, outputs, context):
    errors = []
    commands = CASE_COMMANDS.get(case_id)
    if commands is None or not isinstance(outputs, list) or len(outputs) != len(commands):
        return ['missing/unknown case or incomplete command set']
    for output, command in zip(outputs, commands):
        if output.get('argv') != command:
            errors.append('executed command differs from the required real CLI argv')
        if type(output.get('returncode')) is not int or output['returncode'] != 0 or output.get('timed_out'):
            errors.append('CLI subprocess did not exit successfully')
        if not isinstance(output.get('stdout'), str) or not output['stdout'].strip():
            errors.append('CLI produced no functional output')
    if errors:
        return errors
    texts = [output['stdout'] for output in outputs]
    lines = [nonempty_lines(text) for text in texts]
    try:
        if case_id == 'cli:pkg/list':
            valid = exact_set(lines[0], context['packages'])
        elif case_id == 'cli:pkg/prefix':
            marker = Path(context['prefix']) / 'share/ament_index/resource_index/packages/rmw_mdds'
            valid = lines[0] == [context['prefix']] and marker.is_file()
        elif case_id == 'cli:pkg/xml':
            root = ET.fromstring(texts[0])
            valid = (xml_structure(root) == xml_structure(ET.fromstring(context['package_xml'])) and
                     root.findtext('name') == 'rmw_mdds' and
                     'rmw_implementation_packages' in [element.text for element in root.findall('member_of_group')])
        elif case_id == 'cli:pkg/executables':
            valid = all(exact_set(actual, context['executables'][package]) and
                        all(Path(path).is_file() for path in actual)
                        for package, actual in zip(('demo_nodes_cpp', 'demo_nodes_py'), lines))
        elif case_id == 'cli:interface/list':
            sections, current = {}, None
            for line in texts[0].splitlines():
                if not line.strip():
                    continue
                if line in ('Messages:', 'Services:', 'Actions:'):
                    current = line[:-1]
                    if current in sections:
                        raise ValueError('duplicate interface section')
                    sections[current] = []
                elif line.startswith('    ') and current:
                    sections[current].append(line.strip())
                else:
                    raise ValueError('unexpected interface list line')
            valid = set(sections) == set(context['interfaces']) and all(
                exact_set(sections[key], value) for key, value in context['interfaces'].items())
        elif case_id == 'cli:interface/package':
            valid = exact_set(lines[0], context['package_interfaces'])
        elif case_id == 'cli:interface/packages':
            valid = exact_set(lines[0], context['interface_packages'])
        elif case_id == 'cli:interface/show':
            valid = lines == SHOW_FIELDS
        elif case_id == 'cli:interface/proto':
            import yaml
            try:
                value = yaml.safe_load(texts[0])
            except yaml.YAMLError as exc:
                raise ValueError(f'invalid prototype YAML: {exc}') from exc
            valid = value == {'data': ''} and type(value['data']) is str
        elif case_id == 'cli:extension_points':
            names = []
            for line in texts[0].splitlines():
                match = re.fullmatch(r'([^\s:]+):(?: .*)?', line)
                if match is None:
                    raise ValueError('unloaded or malformed extension point')
                names.append(match[1])
            valid = exact_set(names, context['extension_points'])
        elif case_id == 'cli:extensions':
            sections, current = {}, None
            for line in texts[0].splitlines():
                if not line.strip():
                    continue
                if re.fullmatch(r'[^\s:]+', line):
                    if line in sections:
                        raise ValueError('duplicate extension group')
                    current = line
                    sections[current] = []
                else:
                    match = re.fullmatch(r'  ([^\s:]+):(?: .*)?', line)
                    if match is None or current is None:
                        raise ValueError('unloaded or malformed CLI extension')
                    sections[current].append(match[1])
            valid = set(sections) == set(context['extensions']) and all(
                exact_set(sections[key], value) for key, value in context['extensions'].items())
        elif case_id == 'cli:plugin/list':
            valid = (lines[0] == [context['plugin_package'] + ':'] + context['plugin_lines'])
        else:
            valid = False
        if not valid:
            errors.append('functional output differs from the complete installed-artifact oracle')
    except (ValueError, KeyError, TypeError, ET.ParseError, ImportError) as exc:
        errors.append(str(exc))
    return errors


def execute(argv, env, timeout):
    actual = ([sys.executable, '-B', '-c',
               'from ros2cli.cli import main; raise SystemExit(main())'] + argv[1:]
              if argv and argv[0] == 'ros2' else list(argv))
    environment = os.environ.copy()
    environment.update(env)
    timed_out = False
    with subprocess.Popen(actual, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          env=environment, start_new_session=(os.name == 'posix')) as process:
        def stop_child():
            try:
                if os.name == 'posix':
                    os.killpg(process.pid, signal.SIGKILL)
                else:
                    process.kill()
            except ProcessLookupError:
                pass

        def stop_on_signal(signum, frame):
            stop_child()
            process.wait()
            raise SystemExit(128 + signum)

        previous = {number: signal.signal(number, stop_on_signal)
                    for number in (signal.SIGINT, signal.SIGTERM)}
        try:
            try:
                stdout, stderr = process.communicate(timeout=timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
                stop_child()
                stdout, stderr = process.communicate()
        finally:
            for number, handler in previous.items():
                signal.signal(number, handler)
        return {'argv': list(argv), 'actual_argv': actual, 'returncode': process.returncode,
                'stdout': stdout.decode('utf-8', errors='replace'),
                'stderr': stderr.decode('utf-8', errors='replace'), 'timed_out': timed_out}


def claim_output(prefix, run_id, output):
    prefix, output = Path(prefix).resolve(), Path(output)
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]*', run_id):
        raise ValueError('unsafe run ID')
    run_root = prefix / '.mdds-owned-runs' / run_id
    if output.absolute() != run_root / 'cli_metadata' or output.resolve() != run_root / 'cli_metadata':
        raise ValueError('output must be this run-owned cli_metadata directory')
    owner = run_root / 'owner'
    if run_root.is_symlink() or not owner.is_file() or owner.is_symlink():
        raise ValueError('missing exact run owner')
    if not re.fullmatch(r'MDDS_RUN_OWNER RUN_ID=' + re.escape(run_id) +
                        r' LABEL=[A-Za-z0-9_.-]+\n?', owner.read_text()):
        raise ValueError('wrong run owner identity')
    output.mkdir(mode=0o700)
    return output


def write_json(path, value):
    with Path(path).open('x', encoding='utf-8', newline='\n') as output:
        json.dump(value, output, indent=2, sort_keys=True)
        output.write('\n')
    return {'path': Path(path).name, 'sha256': sha(Path(path).read_bytes())}


def run_cases(template, context, output, run_id, board, env, executor=execute):
    if board != acceptance.TARGET['board_serials'][0]:
        raise ValueError('this first metadata batch requires the manifest board A execution evidence')
    manifest = copy.deepcopy(template)
    manifest['run_id'] = run_id
    for case in manifest['cases']:
        case['status'], case['evidence'] = 'NOT_RUN', []
    cases = {case['id']: case for case in manifest['cases']}
    if not set(CASE_COMMANDS) <= set(cases):
        raise ValueError('standard manifest is missing selected CLI cases')
    context_ref = write_json(output / 'oracle_context.json', context)
    for case_id, commands in CASE_COMMANDS.items():
        observations = []
        for command in commands:
            try:
                observations.append(executor(command, env, 40))
            except OSError as exc:
                observations.append({'argv': command, 'actual_argv': [], 'returncode': 127,
                                     'stdout': '', 'stderr': str(exc), 'timed_out': False})
        errors = check_case(case_id, observations, context)
        passed = not errors
        case = cases[case_id]
        case['status'] = 'PASS' if passed else 'FAIL'
        token = case_id.replace(':', '_').replace('/', '_')
        assertion = f'MDDS_CLI_FUNCTIONAL CASE={case_id} RESULT=PASS EXECUTIONS={len(commands)}'
        executions = []
        for index, observation in enumerate(observations):
            text = ('MDDS_CLI_ACTUAL_ARGV ' + json.dumps(observation['actual_argv']) + '\n'
                    'MDDS_CLI_STDOUT_BEGIN\n' + observation['stdout'] + '\nMDDS_CLI_STDOUT_END\n'
                    'MDDS_CLI_STDERR_BEGIN\n' + observation['stderr'] + '\nMDDS_CLI_STDERR_END\n' +
                    acceptance.terminal_marker(run_id, case_id, observation['returncode'],
                                               observation['argv'], board) + '\n')
            if passed and index == 0:
                text += assertion + '\n'
            path = output / f'{token}.{index}.log'
            with path.open('x', encoding='utf-8', newline='\n') as log:
                log.write(text)
            executions.append({'argv': observation['argv'], 'actual_argv': observation['actual_argv'],
                               'returncode': observation['returncode'], 'board_serial': board,
                               'timed_out': observation['timed_out'],
                               'log': {'path': path.name, 'sha256': sha(path.read_bytes())}})
        receipt = {'schema_version': 1, 'run_id': run_id, 'case_id': case_id,
                   'kind': 'functional', 'status': case['status'], 'board_serials': acceptance.TARGET['board_serials'],
                   'rmw_implementation': 'rmw_mdds', 'transport': 'dsoftbus',
                   'oracle_context': context_ref, 'errors': errors, 'executions': executions,
                   'assertions': [{'id': name, 'passed': passed, 'execution': 0,
                                   'pattern': assertion if passed else ''} for name in case['assertions']]}
        case['evidence'] = [write_json(output / (token + '.receipt.json'), receipt)]
        if passed:
            acceptance.validate_receipt(case, case['evidence'][0], manifest, output)
    write_json(output / 'partial_manifest.json', manifest)
    return manifest


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', type=Path, required=True)
    parser.add_argument('--run-id', required=True)
    parser.add_argument('--board-serial', choices=acceptance.TARGET['board_serials'][:1], required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, default=Path(__file__).with_name('cli_acceptance_manifest.json'))
    args = parser.parse_args(argv)
    try:
        profile = {'RMW_IMPLEMENTATION': 'rmw_mdds', 'MDDS_DEPLOYMENT_PROFILE': 'ohos_dsoftbus',
                   'ROS_AUTOMATIC_DISCOVERY_RANGE': 'SYSTEM_DEFAULT'}
        if any(os.environ.get(key) != value for key, value in profile.items()) or 'MDDS_TRANSPORT' in os.environ:
            raise ValueError('parent must source the OHOS DSoftBus deployment profile before this runner')
        template = json.loads(args.manifest.read_text(encoding='utf-8'))
        inventory = acceptance.discover_inventory(args.prefix)
        expected = acceptance.make_manifest(inventory)
        if template != expected:
            raise ValueError('installed inventory or acceptance template differs from the pinned standard')
        context = collect_context(args.prefix)
        output = claim_output(args.prefix, args.run_id, args.output_dir)
        environment = {'AMENT_PREFIX_PATH': str(args.prefix.resolve()), 'PYTHONDONTWRITEBYTECODE': '1'}
        for key, leaf in {'HOME': 'home', 'ROS_HOME': 'ros_home', 'XDG_CONFIG_HOME': 'config',
                          'XDG_CACHE_HOME': 'cache', 'TMPDIR': 'tmp'}.items():
            path = output / leaf
            path.mkdir(mode=0o700)
            environment[key] = str(path)
        partial = run_cases(template, context, output, args.run_id, args.board_serial, environment)
        report = acceptance.validate_manifest(partial, inventory, output, phase=2)
        write_json(output / 'phase_gate.json', report)
        failed = [case['id'] for case in partial['cases'] if case['status'] == 'FAIL']
        summary = {'selected_cases': len(CASE_COMMANDS), 'passed_cases': len(CASE_COMMANDS) - len(failed),
                   'failed_cases': failed, 'phase1_pass': report['phase1_pass'],
                   'gateway_unlocked': report['gateway_unlocked'], 'output_dir': str(output)}
        write_json(output / 'summary.json', summary)
        print(json.dumps(summary, sort_keys=True))
        return 1 if failed else 0
    except (ValueError, OSError, configparser.Error) as exc:
        print(f'METADATA_ACCEPTANCE_ERROR: {exc}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
