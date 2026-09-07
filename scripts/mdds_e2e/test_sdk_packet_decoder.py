"""Independent byte fixtures for SDK capture, broker envelope and CDR decoding."""
import struct
import unittest
from sdk_packet_decoder import parse_trace,decode_packet

def packet():
    text=b'run|nonce|payload';cdr=b'\0\1\0\0'+struct.pack('<I',len(text)+1)+text+b'\0'
    body=b'A'*16+b'B'*16+struct.pack('!QQI',7,123456789,len(cdr))+cdr
    inner=b'MDDS'+bytes([10,1])+struct.pack('!HI',1,len(body))+body
    outer=b'MDBR'+bytes([1,1,0,0])+struct.pack('!II',len(inner),175)+b'C'*16+struct.pack('!QQ',1,2)+b'D'*16+struct.pack('!QII',9,len(inner),0)
    return outer+inner
def trace(data,failed=0):return b'STC1'+struct.pack('!III',1,failed,3)+b'run'+struct.pack('!IiiI',2,8,0,len(data))+data

class PacketDecoder(unittest.TestCase):
    def test_exact_nested_payload(self):
        values=parse_trace(trace(packet()),'run');self.assertEqual(len(values),1)
        decoded=decode_packet(values[0]['data']);self.assertEqual(decoded['payload'],'run|nonce|payload');self.assertEqual(decoded['sequence'],7);self.assertEqual(decoded['writer'],(b'A'*16).hex())
    def test_incomplete_capture_and_failure_rejected(self):
        for data in (trace(packet())[:-1],trace(packet(),1),trace(packet())+b'x'):
            with self.assertRaises(ValueError):parse_trace(data,'run')
    def test_wrong_units_or_fragment_cannot_be_misread(self):
        for index,value in ((84,9),(76,1)):
            data=bytearray(packet());data[index]=value
            with self.assertRaises(ValueError):decode_packet(bytes(data))
    def test_cdr_length_must_match(self):
        data=bytearray(packet());data[80+64+4:80+64+8]=struct.pack('<I',999)
        with self.assertRaises(ValueError):decode_packet(bytes(data))

if __name__=='__main__':unittest.main()
