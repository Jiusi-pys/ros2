import unittest
from cli_components import oracle,TYPES


class ComponentOracle(unittest.TestCase):
    def test_types(self):self.assertTrue(oracle('\n'.join(TYPES),{'kind':'types'}))
    def test_missing_type(self):self.assertFalse(oracle('\n'.join(TYPES[:-1]),{'kind':'types'}))
    def test_load(self):self.assertTrue(oracle('1  /fixture/primary_B\n',{'kind':'rows','rows':[[1,'/fixture/primary_B']]}))
    def test_wrong_id(self):self.assertFalse(oracle('2  /fixture/primary_B\n',{'kind':'rows','rows':[[1,'/fixture/primary_B']]}))
    def test_wrong_owner(self):self.assertFalse(oracle('1  /fixture/primary_A\n',{'kind':'rows','rows':[[1,'/fixture/primary_B']]}))
    def test_containers(self):self.assertTrue(oracle('/a\n/b\n',{'kind':'names','names':['/a','/b']}))
    def test_unload(self):self.assertTrue(oracle('1\n',{'kind':'id','id':1}))
    def test_empty(self):self.assertTrue(oracle('',{'kind':'rows','rows':[]}))
    def test_not_empty(self):self.assertFalse(oracle('1 /remaining\n',{'kind':'rows','rows':[]}))


if __name__=='__main__':unittest.main()
