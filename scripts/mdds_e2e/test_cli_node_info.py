import copy
import unittest
from cli_node_info import contract,oracle


def render(value):
    lines=[value['node']]
    for section,fields in value['sections'].items():
        lines.append('  '+section+':')
        lines.extend('    '+name+': '+kind for name,kind in fields.items())
    return '\n'.join(lines)+'\n'


class NodeInfoOracle(unittest.TestCase):
    def setUp(self):self.value=contract('/fixture','B','alpha',True)
    def test_complete(self):self.assertTrue(oracle(render(self.value),self.value))
    def test_other_context(self):
        value=contract('/fixture','B','beta');self.assertTrue(oracle(render(value),value))
    def test_duplicate(self):
        value=contract('/fixture','B','duplicate');self.assertTrue(oracle(render(value),value))
    def test_wrong_owner(self):
        value=copy.deepcopy(self.value);value['node']='/fixture/alpha_A';self.assertFalse(oracle(render(value),self.value))
    def test_missing_action(self):
        value=copy.deepcopy(self.value);value['sections']['Action Servers'].clear();self.assertFalse(oracle(render(value),self.value))
    def test_wrong_client(self):
        value=copy.deepcopy(self.value);value['sections']['Action Clients']={'/fixture/A/alpha/action':'example_interfaces/action/Fibonacci'};self.assertFalse(oracle(render(value),self.value))
    def test_ghost_endpoint(self):
        value=copy.deepcopy(self.value);value['sections']['Publishers']['/ghost']='std_msgs/msg/String';self.assertFalse(oracle(render(value),self.value))
    def test_missing_section(self):
        value=copy.deepcopy(self.value);value['sections'].pop('Service Clients');self.assertFalse(oracle(render(value),self.value))
    def test_duplicate_field(self):
        self.assertFalse(oracle(render(self.value)+'    /fixture/A/beta/action: example_interfaces/action/Fibonacci\n',self.value))


if __name__=='__main__':unittest.main()
