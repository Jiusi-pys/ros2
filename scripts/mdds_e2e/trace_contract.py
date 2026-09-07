"""Independent decoded CTF contract for tracing lifecycle observations."""
import re


def validate_events(decoded, actors):
    if set(decoded) != {'lifecycle', 'interactive'} or set(actors) != {
            'active', 'paused', 'resumed', 'stopped', 'interactive'}:
        raise ValueError('incomplete trace sessions/actors')
    for session, raw in decoded.items():
        observed = []
        for line in raw.splitlines():
            if 'ros2:rcl_node_init:' not in line:
                continue
            name = re.search(r'\bnode_name = "([^"]+)"', line)
            pid = re.search(r'\bvpid = ([0-9]+)\b', line)
            if not name or not pid:
                raise ValueError('node trace lacks name or process identity')
            observed.append((name[1], int(pid[1])))
        phases = ('active', 'resumed') if session == 'lifecycle' else ('interactive',)
        expected = [(actors[phase]['node'], actors[phase]['pid']) for phase in phases]
        if sorted(observed) != sorted(expected):
            raise ValueError(f'{session} trace events differ: {observed}; expected {expected}')
