"""Independent payload and storage checks for recorded ROS String fixtures."""
import sqlite3
import struct
from contextlib import closing

FORMATS=('sqlite3','mcap')


def topic(run,role,storage,kind):return '/bags_'+run+'/'+role+'/'+storage+'/'+kind


def payloads(run,nonce,source,storage,kind):return [f'{run}|{nonce}|{source}|{storage}|{kind}|{i}' for i in range(5)]


def decode_string(data):
    if len(data)<9 or data[:4]!=b'\0\1\0\0':raise ValueError('wrong CDR encapsulation/length')
    size=struct.unpack_from('<I',data,4)[0]
    if size<1 or len(data)!=8+size or data[-1]!=0:raise ValueError('invalid CDR String length/terminator')
    return data[8:-1].decode('utf-8')


def read_sqlite(path):
    with closing(sqlite3.connect(path.resolve().as_uri()+'?mode=ro',uri=True)) as db:
        rows=db.execute('SELECT topics.name, messages.timestamp, messages.data FROM messages JOIN topics ON topics.id=messages.topic_id ORDER BY messages.timestamp,messages.id').fetchall()
        types=db.execute('SELECT name,type,serialization_format FROM topics ORDER BY name').fetchall()
    return types,[(name,time,decode_string(data)) for name,time,data in rows]


def read_mcap(path):
    raw=path.read_bytes();magic=b'\x89MCAP0\r\n'
    if not raw.startswith(magic) or not raw.endswith(magic):raise ValueError('MCAP magic missing')
    channels={};schemas={};messages=[];offset=8;footer=False;data_end=False
    def string(body,at):
        if at+4>len(body):raise ValueError('short MCAP string')
        size=struct.unpack_from('<I',body,at)[0];at+=4
        if size>len(body)-at:raise ValueError('MCAP string exceeds record')
        return body[at:at+size].decode(),at+size
    while offset<len(raw)-8:
        if offset+9>len(raw)-8:raise ValueError('short MCAP record')
        opcode=raw[offset];size=struct.unpack_from('<Q',raw,offset+1)[0];offset+=9
        if size>len(raw)-8-offset:raise ValueError('MCAP record exceeds file')
        body=raw[offset:offset+size];offset+=size
        if opcode==3:
            sid=struct.unpack_from('<H',body)[0];name,at=string(body,2);schemas[sid]=name
        elif opcode==4:
            cid,sid=struct.unpack_from('<HH',body);name,at=string(body,4);encoding,at=string(body,at);channels[cid]=(name,sid,encoding)
        elif opcode==5:
            if data_end or len(body)<22:raise ValueError('invalid MCAP message location/length')
            cid,sequence,log_time,publish_time=struct.unpack_from('<HIQQ',body)
            messages.append((cid,log_time,decode_string(body[22:])))
        elif opcode==6:raise ValueError('fixture unexpectedly uses chunking')
        elif opcode==15:data_end=True
        elif opcode==2:footer=True
    if not footer or not data_end:raise ValueError('MCAP not finalized')
    types=sorted((name,schemas[sid],encoding) for name,sid,encoding in channels.values())
    return types,[(channels[cid][0],stamp,data) for cid,stamp,data in messages]
