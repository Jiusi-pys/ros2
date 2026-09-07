"""Availability needs compatible request and response paths, with real controls."""
import unittest
from service_qos_contract import expected, validate


class ServiceQoSContractTest(unittest.TestCase):
    def setUp(self): self.value=expected('fixture','a'*32,'A')
    def check(self): validate(self.value,'fixture','a'*32,'A')
    def test_complete_matrix(self): self.check()
    def test_request_only_match_is_not_available(self):
        self.value['ready_checks'][0]['bad_response']=True
        with self.assertRaises(ValueError): self.check()
    def test_response_only_match_is_not_available(self):
        self.value['ready_checks'][1]['bad_request']=True
        with self.assertRaises(ValueError): self.check()
    def test_no_provider_is_not_a_valid_negative_control(self):
        next(iter(self.value['counts'].values()))['servers']=0
        with self.assertRaises(ValueError): self.check()
    def test_positive_response_must_be_exact(self):
        self.value['calls'][0]['sum']+=1
        with self.assertRaises(ValueError): self.check()
    def test_missing_node_ownership(self):
        self.value['services']={}
        with self.assertRaises(ValueError): self.check()
    def test_sibling_node_cannot_inherit_service_ownership(self):
        self.value['nonowners']['duplicate_B']['services']=self.value['services']
        with self.assertRaises(ValueError): self.check()


if __name__=='__main__': unittest.main()
