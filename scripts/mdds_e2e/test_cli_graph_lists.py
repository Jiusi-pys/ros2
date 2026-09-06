import unittest
from cli_graph_lists import expected_rows, list_oracle


class GraphListOracle(unittest.TestCase):
    def check(self, kind, hidden, change=None):
        expected = {'namespace':'/fixture', 'kind':kind, 'hidden':hidden}
        rows = expected_rows('/fixture', kind, hidden)
        lines = [f'{name} [{type_name}]' for name, type_name in rows.items()]
        if change:
            change(lines)
        return list_oracle('\n'.join(lines)+'\n', expected)

    def test_topic_visible(self): self.assertTrue(self.check('topic', False))
    def test_topic_hidden(self): self.assertTrue(self.check('topic', True))
    def test_service_visible(self): self.assertTrue(self.check('service', False))
    def test_service_hidden(self): self.assertTrue(self.check('service', True))
    def test_missing(self): self.assertFalse(self.check('topic', True, lambda lines: lines.pop()))
    def test_duplicate(self): self.assertFalse(self.check('service', True, lambda lines: lines.append(lines[0])))
    def test_hidden_leak(self):
        self.assertFalse(self.check('topic', False, lambda lines: lines.append('/fixture/A/_hidden [std_msgs/msg/Bool]')))
    def test_raw_service_leak(self):
        self.assertFalse(self.check('topic', True, lambda lines: lines.append('rq/fixture/A/serveRequest [example_interfaces/srv/AddTwoInts_Request]')))
    def test_wrong_type(self):
        self.assertFalse(self.check('topic', True, lambda lines: lines.__setitem__(0, lines[0].replace('String','Int32'))))
    def test_cli_hidden_service(self):
        self.assertTrue(self.check('service', True, lambda lines: lines.append('/_ros2cli_123/get_type_description [type_description_interfaces/srv/GetTypeDescription]')))
    def test_global_topic(self):
        self.assertTrue(self.check('topic', False, lambda lines: lines.append('/rosout [rcl_interfaces/msg/Log]')))
    def test_unexpected_namespace(self):
        self.assertFalse(self.check('service', True, lambda lines: lines.append('/foreign/serve [example_interfaces/srv/AddTwoInts]')))


if __name__ == '__main__': unittest.main()
