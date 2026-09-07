import struct
import unittest
from bag_contract import decode_string


class BagCdr(unittest.TestCase):
    def encoded(self,text):
        data=text.encode()+b'\0';return b'\0\1\0\0'+struct.pack('<I',len(data))+data
    def test_string(self):self.assertEqual(decode_string(self.encoded('run|nonce|0')),'run|nonce|0')
    def test_unicode(self):self.assertEqual(decode_string(self.encoded('测试')), '测试')
    def test_truncated(self):
        with self.assertRaises(ValueError):decode_string(self.encoded('abc')[:-1])
    def test_wrong_encapsulation(self):
        with self.assertRaises(ValueError):decode_string(b'junk'+self.encoded('abc')[4:])
    def test_no_terminator(self):
        with self.assertRaises(ValueError):decode_string(self.encoded('abc')[:-1]+b'x')
    def test_trailing_bytes(self):
        with self.assertRaises(ValueError):decode_string(self.encoded('abc')+b'extra')


if __name__=='__main__':unittest.main()
