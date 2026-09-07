"""Isolation must retain real local traffic and an enabled peer control."""
import unittest
from discovery_off_contract import specs,expected_nodes,validate_counts,validate_data,payloads

class DiscoveryOffContract(unittest.TestCase):
    def test_expected_context_cardinality(self):
        self.assertEqual(len(specs('run','A','off')),16)
        self.assertEqual(len(specs('run','A','on')),13)
        self.assertEqual(len(expected_nodes('run','A','off')),2)
        self.assertEqual(len(expected_nodes('run','A','on')),2)
    def test_counts_are_not_zero_for_live_local_entities(self):
        validate_counts({'off_publishers':1,'off_subscriptions':1,'off_writer_matches':1,'off_reader_matches':1,'on_publishers':2,'on_subscriptions':2,'on_writer_matches':2,'on_reader_matches':2})
        with self.assertRaises(ValueError):validate_counts({})
    def test_off_local_and_on_peer_payloads_are_required(self):
        off=payloads('run','a'*32,'A','off');on=payloads('run','a'*32,'A','on')+payloads('run','a'*32,'B','on')
        validate_data({'off':off,'on':on},'run','a'*32,'A')
        with self.assertRaises(ValueError):validate_data({'off':off,'on':[]},'run','a'*32,'A')
        with self.assertRaises(ValueError):validate_data({'off':off+[on[0]],'on':on},'run','a'*32,'A')

if __name__=='__main__':unittest.main()
