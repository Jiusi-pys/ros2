"""Mutate real SDK evidence copies; incomplete transport proof must fail."""
import json
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import unittest
from cli_acceptance import TARGET
from sdk_packet_decoder import parse_trace
from verify_socket_packets import emit,packet_line
SOURCE=Path(sys.argv[1]).resolve();A,B=TARGET['board_serials']

class SocketReceipt(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)/SOURCE.name
        shutil.copytree(SOURCE,self.root,ignore=shutil.ignore_patterns('*.tar','*.py','mdds_token_exec'))
    def tearDown(self):self.temp.cleanup()
    def reject(self):
        with self.assertRaises((ValueError,KeyError,FileNotFoundError)):emit(self.root)
    def json_change(self,name,change):
        p=self.root/name;v=json.loads(p.read_bytes());change(v);p.write_text(json.dumps(v),encoding='utf-8')
    def log_change(self,name,old,new):
        p=self.root/name;p.write_text(p.read_text(encoding='utf-8').replace(old,new),encoding='utf-8')
    def record_change(self,board,change):
        p=self.root/(board+'.sdk_packets.bin');data=p.read_bytes();length=struct.unpack_from('!I',data,12)[0]
        marker=data[16:16+length].decode();records=parse_trace(data,marker)
        indices=[i for i,r in enumerate(records) if r['direction']==(1 if board==A else 2) and b'|alpha|1|0' in r['data']]
        self.assertTrue(indices)
        for index in indices:
            old=packet_line(index,records[index]);change(records[index]);new=packet_line(index,records[index])
            self.log_change(board+'.daemon.log',old,new)
        # Keep native binary/text representations consistent to reach semantic checks.
        p.write_bytes(data[:16+length]+b''.join(struct.pack('!IiiI',r['direction'],r['fd'],r['result'],len(r['data']))+r['data'] for r in records))
    def test_original(self):self.assertEqual(set(emit(self.root)),{'transport:a_to_b','transport:b_to_a'})
    def test_capture_failure(self):
        p=self.root/(A+'.sdk_packets.bin');v=bytearray(p.read_bytes());v[11]=1;p.write_bytes(v);self.reject()
    def test_truncated_capture(self):
        p=self.root/(B+'.sdk_packets.bin');p.write_bytes(p.read_bytes()[:-1]);self.reject()
    def test_raw_send_required(self):self.log_change(A+'.daemon.log','SDK_PACKET index=','REMOVED index=');self.reject()
    def test_raw_receive_required(self):self.log_change(B+'.daemon.log','SDK_PACKET index=','REMOVED index=');self.reject()
    def test_ros_callback_required(self):self.log_change(B+'.ros.log','SDK_ROS_RX ','REMOVED ');self.reject()
    def test_ros_publish_required(self):self.log_change(A+'.ros.log','SDK_ROS_TX ','REMOVED ');self.reject()
    def test_terminal_required(self):self.log_change(B+'.daemon.log','MDDS_CLI_TERMINAL ','REMOVED ');self.reject()
    def test_listen_required(self):self.log_change(A+'.daemon.log','[mdds/dsoftbus] Listen(','REMOVED(');self.reject()
    def test_payload_mutation(self):
        self.record_change(B,lambda r:r.update(data=r['data'].replace(b'|alpha|1|0',b'|alpha|1|9')));self.reject()
    def test_writer_mutation(self):
        self.record_change(A,lambda r:r.update(data=r['data'][:92]+bytes([r['data'][92]^1])+r['data'][93:]));self.reject()
    def test_failed_send_is_not_delivery(self):self.record_change(A,lambda r:r.update(result=-7));self.reject()
    def test_unbound_channel(self):self.record_change(B,lambda r:r.update(fd=999));self.reject()
    def test_native_udp_rejected(self):self.json_change(A+'.daemon.inspect.json',lambda v:v.update(owned_udp=['unexpected']));self.reject()
    def test_native_binary_required(self):self.json_change(A+'.daemon.inspect.json',lambda v:v.update(binary_sha256='0'*64));self.reject()
    def test_native_identity_required(self):self.json_change(A+'.daemon.status.json',lambda v:v.update(child_start='1'));self.reject()
    def test_frozen_trace_flag_required(self):
        (self.root/(A+'.sdk_trace.enabled')).write_text('wrong');self.reject()

if __name__=='__main__':unittest.main(argv=[sys.argv[0]])
