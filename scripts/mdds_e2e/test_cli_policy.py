import unittest
from cli_policy import parse_policy,validate_policy


class PolicyPermissions(unittest.TestCase):
    def setUp(self):
        self.xml='<policy version="0.2.0"><enclaves><enclave path="/A/alpha"><profiles><profile ns="/fixture" node="alpha_A"><topics publish="ALLOW"><topic>A/out</topic></topics><services reply="ALLOW"><service>~/get_type_description</service></services></profile></profiles></enclave></enclaves></policy>'
        self.expected={('/A/alpha','/fixture','alpha_A'):{('topic','publish'):{'/fixture/A/out'},('service','reply'):{'/fixture/alpha_A/get_type_description'}}}
    def test_expands_relative_and_private_names(self):self.assertEqual(parse_policy(self.xml),self.expected)
    def test_exact_policy(self):validate_policy(self.xml,self.expected)
    def test_wrong_direction(self):
        with self.assertRaises(ValueError):validate_policy(self.xml.replace('publish=','subscribe='),self.expected)
    def test_wildcard_is_not_exact_permission(self):
        with self.assertRaises(ValueError):validate_policy(self.xml.replace('A/out','*'),self.expected)
    def test_wrong_enclave(self):
        with self.assertRaises(ValueError):validate_policy(self.xml.replace('/A/alpha','/B/alpha'),self.expected)
    def test_duplicate_profile(self):
        with self.assertRaises(ValueError):parse_policy(self.xml.replace('</profiles>','<profile ns="/fixture" node="alpha_A"/></profiles>'))


if __name__=='__main__':unittest.main()
