import unittest
import yaml
from cli_parameters import oracle,values,recipe


class ParameterOracle(unittest.TestCase):
    def test_list(self):self.assertTrue(oracle('cli:param/list','  a\n  b\n',['a','b']))
    def test_duplicate_name(self):self.assertFalse(oracle('cli:param/list','a\na\n',['a']))
    def test_integer(self):self.assertTrue(oracle('cli:param/get','Integer value is: 42\n',{'label':'Integer value is:','value':42}))
    def test_boolean_is_not_integer(self):self.assertFalse(oracle('cli:param/get','Integer value is: True\n',{'label':'Integer value is:','value':1}))
    def test_bytes(self):self.assertTrue(oracle('cli:param/get',"Byte values are: [b'\\x00', b'\\xff']\n",{'label':'Byte values are:','value':[0,255]}))
    def test_wrong_array(self):self.assertFalse(oracle('cli:param/get','Integer values are: [1, 2]\n',{'label':'Integer values are:','value':[2,1]}))
    def test_description(self):
        wanted=recipe('/fixture','B','12345678')[10][3]
        self.assertTrue(oracle('cli:param/describe','\n'.join(wanted),wanted))
    def test_dump(self):
        data=values('12345678','B');dump=dict(data);dump['octets']=[bytes([n]) for n in data['octets']]
        self.assertTrue(oracle('cli:param/dump',yaml.safe_dump({'/fixture':{'ros__parameters':dump}}),{'node':'/fixture','values':data}))
        dump['ratio']=1
        self.assertFalse(oracle('cli:param/dump',yaml.safe_dump({'/fixture':{'ros__parameters':dump}}),{'node':'/fixture','values':data}))


if __name__=='__main__':unittest.main()
