"""Adversarial endpoint metadata, matching and data-plane receipt checks."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
import cli_acceptance as a
from service_graph_receipt import emit
from verify_cli_daemon import validate_report

SOURCE=Path(sys.argv[1]).resolve();A,B=a.TARGET['board_serials']


class EndpointQoSReceipt(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='endpoint_qos_receipt_');self.root=Path(self.temp.name)/SOURCE.name
        shutil.copytree(SOURCE,self.root,ignore=shutil.ignore_patterns('*.tar','*.py','mdds_broker_daemon','mdds_token_exec'))
        self.value=json.loads((self.root/(A+'.cli.results.json')).read_bytes());self.nonce=(self.root/'nonce').read_text().strip()
    def tearDown(self):self.temp.cleanup()
    def check(self):
        validate_report(self.value,self.root,SOURCE.name,A,self.nonce)
        reports={A:self.value,B:json.loads((self.root/(B+'.cli.results.json')).read_bytes())}
        manifest=json.loads((self.root/'cli_partial_manifest.json').read_bytes());self.assertEqual(emit(self.root,manifest,reports,SOURCE.name,self.nonce),'graph:endpoint_metadata')
    def reject(self):
        with self.assertRaises(ValueError):self.check()
    def data(self):return self.value['endpoint_qos']
    def test_original(self):self.check()
    def test_incompatible_reader_match(self):self.data()['counts']['reliability_bad']['reader_matches']=1;self.reject()
    def test_incompatible_writer_match(self):self.data()['counts']['durability_bad']['writer_matches']=1;self.reject()
    def test_liveliness_mismatch_cannot_match(self):self.data()['counts']['liveliness_bad']['reader_matches']=1;self.reject()
    def test_lease_mismatch_cannot_deliver(self):self.data()['received']['liveliness_lease_bad']=['unexpected'];self.reject()
    def test_infinite_lease_cannot_satisfy_request(self):self.data()['compatibility']['liveliness_infinite_bad']['code']=0;self.reject()
    def test_manual_positive_control_must_deliver(self):self.data()['received']['liveliness_manual_equal']=[];self.reject()
    def test_one_nanosecond_mismatch_must_reject(self):self.data()['counts']['deadline_precision_bad']['reader_matches']=1;self.reject()
    def test_exact_lifespan_metadata_must_not_round(self):self.data()['metadata']['deadline_precision_compatible']['publisher']['qos']['lifespan']=123_456_000_000;self.reject()
    def test_expired_sample_must_not_deliver(self):self.data()['received']['lifespan_expired']=['expired'];self.reject()
    def test_expiration_is_not_incompatibility(self):self.data()['counts']['lifespan_expired']['reader_matches']=0;self.reject()
    def test_absence_is_not_qos_rejection(self):self.data()['counts']['deadline_bad']['publishers']=0;self.reject()
    def test_incompatible_data_is_rejected(self):self.data()['received']['reliability_bad']=['wrong'];self.reject()
    def test_positive_control_is_required(self):self.data()['received']['reliable']=[];self.reject()
    def test_publisher_qos_must_match(self):self.data()['metadata']['deadline_compatible']['publisher']['qos']['deadline']=2_000_000_000;self.reject()
    def test_endpoint_direction_must_match(self):self.data()['metadata']['reliable']['subscription']['direction']=1;self.reject()
    def test_endpoint_owner_must_match(self):self.data()['metadata']['reliable']['publisher']['node']='alpha_A';self.reject()
    def test_type_hash_must_match(self):self.data()['metadata']['reliable']['publisher']['type_hash']='RIHS01_'+'0'*64;self.reject()
    def test_negative_window_required(self):self.data()['observation_ns']=0;self.reject()
    def test_compatibility_query_cannot_contradict_matching(self):self.data()['compatibility']['deadline_bad']['code']=0;self.reject()
    def test_peer_must_have_published(self):
        p=self.root/(B+'.endpoint_qos.json');value=json.loads(p.read_bytes());value['sent']['reliable']=[];p.write_text(json.dumps(value));self.reject()
    def test_both_boards_must_finish_sending(self):(self.root/(B+'.endpoint_qos.sent')).write_text('old');self.reject()
    def test_raw_receiving_callback_required(self):
        p=self.root/(A+'.ros.log');p.write_text(p.read_text().replace('ENDPOINT_QOS_RX ','REMOVED '));self.reject()
    def test_incompatibility_event_policy_must_match(self):
        p=self.root/(A+'.ros.log');p.write_text(p.read_text().replace('Last incompatible policy: DEADLINE','Last incompatible policy: INVALID'));self.reject()


if __name__=='__main__':unittest.main(argv=[sys.argv[0]])
