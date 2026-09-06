import unittest
from cli_daemon import node_names, oracle


class DaemonOracle(unittest.TestCase):
    def test_started(self):self.assertTrue(oracle('cli:daemon/start','The daemon has been started\n','The daemon has been started'))
    def test_running(self):self.assertTrue(oracle('cli:daemon/status','The daemon is running\n','The daemon is running'))
    def test_stopped(self):self.assertTrue(oracle('cli:daemon/stop','The daemon has been stopped\n','The daemon has been stopped'))
    def test_existing_start(self):self.assertFalse(oracle('cli:daemon/start','The daemon is already running\n','The daemon has been started'))
    def test_nodes(self):self.assertTrue(oracle('cli:node/list','\n'.join(node_names('/fixture')),node_names('/fixture')))
    def test_missing_duplicate(self):self.assertFalse(oracle('cli:node/list','\n'.join(set(node_names('/fixture'))),node_names('/fixture')))
    def test_ghost(self):self.assertFalse(oracle('cli:node/list','\n'.join(node_names('/fixture')+['/ghost']),node_names('/fixture')))
    def test_wrong_namespace(self):self.assertFalse(oracle('cli:node/list','\n'.join(node_names('/other')),node_names('/fixture')))


if __name__ == '__main__': unittest.main()
