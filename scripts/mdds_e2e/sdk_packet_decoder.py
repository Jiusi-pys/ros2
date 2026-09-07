"""Strict decoder for bounded SDK traces and small full-frame String samples."""
import hashlib
import struct

def parse_trace(data,marker):
    if len(data)>2*1024*1024 or len(data)<16 or data[:4]!=b'STC1':raise ValueError('invalid SDK trace header/bound')
    count,failed,length=struct.unpack_from('!III',data,4)
    if failed or count>65536 or not 1<=length<=128 or data[16:16+length]!=marker.encode():raise ValueError('SDK trace failed or marker differs')
    offset=16+length;records=[]
    for _ in range(count):
        if len(data)-offset<16:raise ValueError('truncated SDK record')
        direction,fd,result,size=struct.unpack_from('!IiiI',data,offset);offset+=16
        if direction not in (1,2) or not 1<=size<=4096 or size>len(data)-offset:raise ValueError('invalid SDK record')
        packet=data[offset:offset+size];offset+=size
        if marker.encode() not in packet:raise ValueError('SDK record lacks selected marker')
        records.append({'direction':direction,'fd':fd,'result':result,'data':packet})
    if offset!=len(data):raise ValueError('SDK trace trailing bytes')
    return records

def decode_packet(data):
    if len(data)<144 or data[:8]!=b'MDBR\x01\x01\0\0':raise ValueError('wrong broker fragment')
    size,domain=struct.unpack_from('!II',data,8);source,destination=struct.unpack_from('!QQ',data,32);transfer,total,offset=struct.unpack_from('!QII',data,64)
    if size!=len(data)-80 or total!=size or offset!=0 or domain!=175 or not 0<source<(1<<56) or not 0<destination<(1<<56) or not transfer or not any(data[16:32]) or not any(data[48:64]):raise ValueError('incomplete or invalid broker frame')
    inner=data[80:];flags,body_size=struct.unpack_from('!HI',inner,6)
    if inner[:6]!=b'MDDS\x0a\x01' or body_size!=len(inner)-12 or flags&~3 or not flags&1:raise ValueError('wrong MDDS DATA frame')
    writer,epoch=inner[12:28],inner[28:44];sequence,published,size=struct.unpack_from('!QQI',inner,44)
    if not any(writer) or not any(epoch) or not sequence or not published or size>len(inner)-64:raise ValueError('invalid MDDS sample identity')
    cdr=inner[64:64+size]
    if len(cdr)<9 or cdr[:4] not in (b'\0\1\0\0',b'\0\0\0\0'):raise ValueError('wrong String CDR encapsulation')
    length=struct.unpack_from('<I' if cdr[1]==1 else '>I',cdr,4)[0]
    if length<1 or length!=len(cdr)-8 or cdr[-1]!=0:raise ValueError('wrong String CDR length')
    try:payload=cdr[8:-1].decode('utf-8')
    except UnicodeDecodeError as error:raise ValueError('invalid String UTF-8') from error
    return {'payload':payload,'writer':writer.hex(),'epoch':epoch.hex(),'sequence':sequence,'published_ns':published,
            'packet_sha256':hashlib.sha256(data).hexdigest(),'source_port':source,'destination_port':destination,'transfer_id':transfer,
            'broker_epoch':data[16:32].hex(),'link_nonce':data[48:64].hex()}
