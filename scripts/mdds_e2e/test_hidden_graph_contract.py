"""Default CLI views and raw graph names have different contracts."""
import unittest


class HiddenGraphContractTest(unittest.TestCase):
    def test_exact_visible_and_hidden_cardinality(self):
        from hidden_graph_contract import views
        for kind,visible,total in (('node',2,4),('topic',2,18),('service',4,28),('action',2,6)):
            self.assertEqual(len(views('run',kind,False)),visible)
            self.assertEqual(len(views('run',kind,True)),total)
    def test_visible_node_has_hidden_resources(self):
        from hidden_graph_contract import views
        self.assertIn('/hidden_graph_run/A/_secret/action',views('run','action',True))
        self.assertNotIn('/hidden_graph_run/A/_secret/action',views('run','action',False))
    def test_native_view_preserves_service_transport_names(self):
        from hidden_graph_contract import native_topics
        value=native_topics('run')
        self.assertIn('rq/hidden_graph_run/A/_secret/serveRequest',value)
        self.assertIn('rr/hidden_graph_run/A/_hidden/serveReply',value)
        self.assertIn('rt/hidden_graph_run/A/visible/out',value)
        self.assertEqual(len(value),74)


if __name__=='__main__':unittest.main()
