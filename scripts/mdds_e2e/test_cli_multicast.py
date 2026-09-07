import unittest
from cli_multicast import received_packet


class MulticastReceipt(unittest.TestCase):
    def test_exact_peer_packet(self):self.assertEqual(received_packet("Waiting for UDP multicast datagram...\nReceived from 192.168.77.202:53001: 'Hello World!'\n",'192.168.77.202'),53001)
    def test_local_loopback_is_not_peer(self):self.assertIsNone(received_packet("Received from 192.168.77.201:53001: 'Hello World!'\n",'192.168.77.202'))
    def test_wrong_payload(self):self.assertIsNone(received_packet("Received from 192.168.77.202:53001: 'wrong'\n",'192.168.77.202'))
    def test_duplicate_receipts(self):self.assertIsNone(received_packet("Received from 192.168.77.202:53001: 'Hello World!'\n"*2,'192.168.77.202'))


if __name__=='__main__':unittest.main()
