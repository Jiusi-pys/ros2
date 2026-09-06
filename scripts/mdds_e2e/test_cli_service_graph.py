import unittest
from cli_service_graph import oracle


class ServiceGraphOracle(unittest.TestCase):
    def test_type(self):self.assertTrue(oracle('cli:service/type','example_interfaces/srv/AddTwoInts\n','example_interfaces/srv/AddTwoInts'))
    def test_wrong_type(self):self.assertFalse(oracle('cli:service/type','other/srv/Service\n','example_interfaces/srv/AddTwoInts'))
    def test_find(self):self.assertTrue(oracle('cli:service/find','/fixture/B\n/fixture/A\n',['/fixture/A','/fixture/B']))
    def test_duplicate(self):self.assertFalse(oracle('cli:service/find','/fixture/A\n/fixture/A\n',['/fixture/A']))
    def test_missing(self):self.assertFalse(oracle('cli:service/find','/fixture/A\n',['/fixture/A','/fixture/B']))
    def test_count(self):self.assertTrue(oracle('cli:service/find','6\n',6))
    def test_wrong_count(self):self.assertFalse(oracle('cli:service/find','5\n',6))
    def test_info(self):self.assertTrue(oracle('cli:service/info','Type: example_interfaces/srv/AddTwoInts\nClients count: 1\nServices count: 1\n',{'Type':'example_interfaces/srv/AddTwoInts','Clients count':'1','Services count':'1'}))
    def test_wrong_clients(self):self.assertFalse(oracle('cli:service/info','Type: example_interfaces/srv/AddTwoInts\nClients count: 0\nServices count: 1\n',{'Type':'example_interfaces/srv/AddTwoInts','Clients count':'1','Services count':'1'}))
    def test_extra_field(self):self.assertFalse(oracle('cli:service/info','Type: T\nType: T\n',{'Type':'T'}))


if __name__=='__main__':unittest.main()
