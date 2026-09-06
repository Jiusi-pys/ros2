"""Inspect the exact run-owned native component container."""
import hashlib
import json
import os
from pathlib import Path
import re
import time
from board_graph_ownership import process_start


def inspect(root,run):
    record=(root/'container.child.pid').read_text().strip()
    match=re.fullmatch('MDDS_OWNED_PROCESS RUN_ID='+re.escape(run)+r' TAG=container_child PID=(\d+) START=(\d+)',record)
    if not match or process_start(int(match[1]))!=match[2]:raise ValueError('wrong container owner')
    return inspect_pid(root,run,int(match[1]))


def inspect_pid(root,run,pid):
    proc=Path('/proc')/str(pid)
    start=process_start(pid)
    executable=str(root/'component_prefix/lib/rclcpp_components/component_container')
    if os.readlink(proc/'exe')!=executable:raise ValueError('wrong native container binary')
    wanted={str(root/'lib/libmdds.so'),str(root/'lib/librmw_mdds.so'),str(root/'component_prefix/lib/libtalker_component.so')}
    deadline=time.monotonic()+5
    while time.monotonic()<deadline:
        paths=set()
        for line in (proc/'maps').read_text().splitlines():
            parts=line.split(None,5)
            if len(parts)==6 and Path(parts[5]).name in ('libmdds.so','librmw_mdds.so','libtalker_component.so'):paths.add(parts[5])
        if paths==wanted:break
        time.sleep(.1)
    else:raise ValueError('container mappings differ from private inputs')
    sockets=set()
    for fd in (proc/'fd').iterdir():
        try:target=os.readlink(fd)
        except FileNotFoundError:continue
        match=re.fullmatch(r'socket:\[(\d+)\]',target)
        if match:sockets.add(match[1])
    udp=[]
    for table in ('udp','udp6'):
        for line in (proc/'net'/table).read_text().splitlines()[1:]:
            parts=line.split()
            if parts[9] in sockets:udp.append(line)
    if udp:raise ValueError('container owns UDP sockets')
    stat=(proc/'stat').read_text().rsplit(')',1)[1].split()
    if process_start(pid)!=start:raise ValueError('container PID reused during inspection')
    return {'run_id':run,'pid':pid,'start':start,'parent_pid':int(stat[1]),'process_group':int(stat[2]),'executable':executable,'argv':(proc/'cmdline').read_bytes().rstrip(b'\0').decode().split('\0'),
            'hashes':{p:hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in sorted(wanted|{executable})},'owned_udp':udp}
