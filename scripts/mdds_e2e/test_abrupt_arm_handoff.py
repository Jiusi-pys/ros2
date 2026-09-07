"""The faster arm handoff retains nonce, identity and byte-hash checks."""
import base64
import hashlib
import json
import unittest
from abrupt_arm_handoff import decode

class ArmHandoff(unittest.TestCase):
    def test_exact_control_payload(self):
        data=json.dumps({'run_id':'run','nonce':'a'*32,'role':'B','armed_ns':123}).encode()
        encoded=base64.b64encode(data).decode();sha=hashlib.sha256(data).hexdigest()
        self.assertEqual(decode(encoded,sha,'run','a'*32,'B'),data)
        for args in ((encoded,'0'*64,'run','a'*32,'B'),(encoded,sha,'run','b'*32,'B'),(encoded,sha,'run','a'*32,'A')):
            with self.assertRaises(ValueError):decode(*args)
    def test_invalid_or_unbounded_payload(self):
        for data in (b'x'*3000,b'{}'):
            with self.assertRaises(ValueError):decode(base64.b64encode(data).decode(),hashlib.sha256(data).hexdigest(),'run','a'*32,'B')

if __name__=='__main__':unittest.main()
