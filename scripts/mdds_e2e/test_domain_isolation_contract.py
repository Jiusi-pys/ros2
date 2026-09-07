"""Identical graph names must remain separated by actual ROS domain IDs."""
import unittest
from domain_isolation_contract import DOMAINS,specs,payloads,validate_data,validate_counts

class DomainIsolationContract(unittest.TestCase):
    def test_both_domains_have_the_same_endpoint_recipe(self):
        self.assertEqual(DOMAINS,(175,176));self.assertEqual(len(specs('run','A')),9)
    def test_same_domain_local_and_peer_data_required(self):
        for domain in DOMAINS:
            expected=payloads('run','a'*32,domain,'A')+payloads('run','a'*32,domain,'B')
            validate_data(expected,'run','a'*32,domain)
            with self.assertRaises(ValueError):validate_data(expected+payloads('run','a'*32,351-domain,'A'),'run','a'*32,domain)
            with self.assertRaises(ValueError):validate_data([],'run','a'*32,domain)
    def test_missing_participant_is_not_isolation(self):
        validate_counts({'publishers':2,'subscriptions':2,'writer_matches':2,'reader_matches':2,'servers':1,'clients':1})
        with self.assertRaises(ValueError):validate_counts({'publishers':1,'subscriptions':1,'writer_matches':0,'reader_matches':0,'servers':0,'clients':0})

if __name__=='__main__':unittest.main()
