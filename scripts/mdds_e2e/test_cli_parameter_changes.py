import unittest
from cli_parameter_changes import oracle


class ParameterChanges(unittest.TestCase):
    def test_set(self):self.assertTrue(oracle('Set parameter successful\n',{'kind':'message','text':'Set parameter successful'}))
    def test_get(self):self.assertTrue(oracle('Integer value is: 12\n',{'kind':'get','label':'Integer value is:','value':12}))
    def test_wrong_get(self):self.assertFalse(oracle('Integer value is: 13\n',{'kind':'get','label':'Integer value is:','value':12}))
    def test_load(self):self.assertTrue(oracle('Set parameter x successful\n',{'kind':'lines','lines':['Set parameter x successful']}))
    def test_failed_load(self):self.assertFalse(oracle('Set parameter x failed\n',{'kind':'lines','lines':['Set parameter x successful']}))
    def test_deleted(self):self.assertTrue(oracle('',{'kind':'absent'}))
    def test_list(self):self.assertTrue(oracle('  a\n  b\n',{'kind':'list','names':['a','b']}))
    def test_undeleted(self):self.assertFalse(oracle('a\nb\nephemeral\n',{'kind':'list','names':['a','b']}))


if __name__=='__main__':unittest.main()
