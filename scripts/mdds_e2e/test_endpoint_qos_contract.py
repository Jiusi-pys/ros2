"""Graph visibility, QoS matching and actual data delivery are separate gates."""
import unittest


class EndpointQoSContractTest(unittest.TestCase):
    def test_reliability_durability_deadline_liveliness_matrix_has_negative_controls(self):
        from endpoint_qos_contract import MATRIX
        self.assertEqual({v['name'] for v in MATRIX if not v['compatible']},{'reliability_bad','durability_bad','deadline_bad','liveliness_bad','liveliness_lease_bad','liveliness_infinite_bad'})
    def test_liveliness_positive_controls_are_explicit(self):
        from endpoint_qos_contract import row
        for name in ('liveliness_compatible','liveliness_manual_equal','liveliness_lease_compatible','liveliness_lease_equal'):
            self.assertTrue(row(name)['compatible'])
        self.assertEqual(row('liveliness_manual_equal')['offered']['liveliness'],3)
        self.assertEqual(row('liveliness_manual_equal')['requested']['liveliness'],3)
    def test_graph_visibility_does_not_imply_match(self):
        from endpoint_qos_contract import validate_counts
        with self.assertRaises(ValueError):validate_counts('reliability_bad',{'publishers':1,'subscriptions':1,'writer_matches':0,'reader_matches':1})
    def test_undiscovered_is_not_negative_control(self):
        from endpoint_qos_contract import validate_counts
        with self.assertRaises(ValueError):validate_counts('durability_bad',{'publishers':0,'subscriptions':1,'writer_matches':0,'reader_matches':0})
    def test_incompatible_sample_is_rejected(self):
        from endpoint_qos_contract import validate_received
        with self.assertRaises(ValueError):validate_received('deadline_bad',['unexpected'],'run','a'*32,'B')
    def test_compatible_control_requires_all_samples(self):
        from endpoint_qos_contract import validate_received
        with self.assertRaises(ValueError):validate_received('reliable',[],'run','a'*32,'B')


if __name__=='__main__':unittest.main()
