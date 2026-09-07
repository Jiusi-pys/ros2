"""Reject trace metadata-only success and incorrect pause/resume event capture."""
import unittest
from trace_contract import validate_events


class TraceContractTest(unittest.TestCase):
    def setUp(self):
        self.actors = {phase: {'node': 'trace_' + phase + '_A', 'pid': 100 + index}
                       for index, phase in enumerate(('active', 'paused', 'resumed', 'stopped', 'interactive'))}
        self.rows = {session: '\n'.join(self.line(phase) for phase in phases)
                     for session, phases in {'lifecycle': ('active', 'resumed'), 'interactive': ('interactive',)}.items()}

    def line(self, phase):
        actor = self.actors[phase]
        return ('[12:00:00.000000000] (+?.?????????) localhost ros2:rcl_node_init: '
                '{ cpu_id = 0 }, { vpid = %d }, { node_name = "%s", namespace = "/" }'
                % (actor['pid'], actor['node']))

    def test_exact_lifecycle(self):
        validate_events(self.rows, self.actors)

    def test_metadata_without_events(self):
        self.rows['lifecycle'] = ''
        with self.assertRaises(ValueError): validate_events(self.rows, self.actors)

    def test_paused_node_is_absent(self):
        self.rows['lifecycle'] += '\n' + self.line('paused')
        with self.assertRaises(ValueError): validate_events(self.rows, self.actors)

    def test_stopped_node_is_absent(self):
        self.rows['lifecycle'] += '\n' + self.line('stopped')
        with self.assertRaises(ValueError): validate_events(self.rows, self.actors)

    def test_pid_is_required(self):
        self.rows['lifecycle'] = self.rows['lifecycle'].replace('vpid = 100', 'vpid = 999')
        with self.assertRaises(ValueError): validate_events(self.rows, self.actors)

    def test_duplicate_event_rejected(self):
        self.rows['lifecycle'] += '\n' + self.line('active')
        with self.assertRaises(ValueError): validate_events(self.rows, self.actors)

    def test_interactive_session_separate(self):
        self.rows['interactive'] = self.rows['lifecycle']
        with self.assertRaises(ValueError): validate_events(self.rows, self.actors)


if __name__ == '__main__': unittest.main()
