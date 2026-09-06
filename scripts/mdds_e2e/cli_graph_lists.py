"""Deterministic fixtures and output checks for visible/hidden graph lists."""
import re


def expected_rows(namespace, kind, hidden):
    rows = {}
    for role in ('A', 'B'):
        for name in ('alpha', 'beta'):
            if kind == 'topic':
                rows[f'{namespace}/{role}/{name}/out'] = 'std_msgs/msg/String'
            else:
                rows[f'{namespace}/{role}/{name}/serve'] = 'example_interfaces/srv/AddTwoInts'
        if kind == 'topic':
            for suffix in ('cli_source', 'cli_sink'):
                rows[f'{namespace}/{role}/{suffix}'] = 'std_msgs/msg/Int32'
            if hidden:
                rows[f'{namespace}/{role}/_hidden'] = 'std_msgs/msg/Bool'
        else:
            for name in ('alpha', 'beta', 'duplicate'):
                rows[f'{namespace}/{name}_{role}/get_type_description'] = 'type_description_interfaces/srv/GetTypeDescription'
            if hidden:
                rows[f'{namespace}/{role}/_hidden_service'] = 'example_interfaces/srv/AddTwoInts'
    return rows


def list_oracle(stdout, expected):
    rows, seen = {}, set()
    namespace, kind, hidden = (expected[key] for key in ('namespace', 'kind', 'hidden'))
    for line in stdout.splitlines():
        if not line.strip(): continue
        match = re.fullmatch(r'(/\S+) \[([^\[\]]+)\]', line.strip())
        if not match: return False
        name, type_name = match.groups()
        if name in seen: return False
        seen.add(name)
        if name.startswith(namespace+'/'):
            rows[name] = type_name
        elif kind == 'topic' and {'/rosout':'rcl_interfaces/msg/Log', '/parameter_events':'rcl_interfaces/msg/ParameterEvent'}.get(name) == type_name:
            continue
        elif kind == 'service' and hidden and re.fullmatch(r'/_ros2cli_[0-9]+/get_type_description', name) and type_name == 'type_description_interfaces/srv/GetTypeDescription':
            continue
        else:
            return False
    return rows == expected_rows(namespace, kind, hidden)
