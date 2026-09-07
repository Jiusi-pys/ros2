"""A cleanup scan may select only the run's exact daemon identities."""
import copy
import unittest
from unittest.mock import patch
import cli_daemon_guard as guard


class CleanupSelectionTest(unittest.TestCase):
    def setUp(self):
        self.root='/data/local/tmp/ros2/.mdds-owned-runs/test'
        self.record={'pid':123,'start':'456','state':'S','argv':['python3.12']+guard.DAEMON_ARGS,
                     'broker_root':self.root+'/brokers','libraries':[self.root+'/lib/libmdds.so',self.root+'/lib/librmw_mdds.so']}
    def test_selects_only_our_run(self):
        foreign=copy.deepcopy(self.record);foreign['pid']=124;foreign['broker_root']='/other/brokers'
        self.assertEqual(guard.select_owned([self.record,foreign],self.root),[self.record])
    def test_rejects_wrong_library_mapping(self):
        self.record['libraries'][0]='/other/libmdds.so'
        self.assertEqual(guard.select_owned([self.record],self.root),[])
    def test_rejects_missing_process_start(self):
        self.record['start']=''
        self.assertEqual(guard.select_owned([self.record],self.root),[])
    def test_cleanup_preserves_foreign_daemon(self):
        foreign=copy.deepcopy(self.record);foreign['broker_root']='/foreign/brokers'
        with patch.object(guard.Path,'read_text',return_value='MDDS_RUN_OWNER RUN_ID=test LABEL=ros_broker\n'), \
             patch.object(guard.Path,'write_text'),patch.object(guard.Path,'replace'), \
             patch.object(guard,'domain_daemons',return_value=[foreign]), \
             patch.object(guard,'retire') as retire,patch.object(guard,'assert_absent') as absent:
            value=guard.cleanup(self.root)
        self.assertEqual(value['foreign_preserved'],[foreign]);self.assertEqual(value['selected'],[])
        retire.assert_not_called();absent.assert_not_called()
    def test_identity_change_before_signal_is_rejected(self):
        changed=copy.deepcopy(self.record);changed['broker_root']='/foreign/brokers'
        with patch.object(guard.Path,'read_text',return_value='MDDS_RUN_OWNER RUN_ID=test LABEL=ros_broker\n'), \
             patch.object(guard,'observe',side_effect=[self.record,changed]),patch.object(guard.os,'kill') as kill:
            with self.assertRaises(ValueError):guard.retire(123,self.root,'456')
        kill.assert_not_called()


if __name__=='__main__':unittest.main()
