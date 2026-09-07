"""Run actual bag recording, inspect its native storage and freeze output files."""
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time
import yaml
import cli_acceptance as a
from bag_contract import topic,payloads
from board_graph_ownership import process_start


def inspect_process(pid,root,storage):
    proc=Path('/proc')/str(pid);paths=set()
    names={'libmdds.so','librmw_mdds.so','librosbag2_storage_'+storage+'.so'}
    for line in (proc/'maps').read_text().splitlines():
        parts=line.split(None,5)
        if len(parts)==6 and Path(parts[5]).name in names:paths.add(parts[5])
    # The deployed lib directory links to Lib; /proc/maps names the target.
    wanted={str(root/'lib/libmdds.so'),str(root/'lib/librmw_mdds.so'),'/data/local/tmp/ros2/Lib/librosbag2_storage_'+storage+'.so'}
    if paths!=wanted:raise ValueError('recorder library paths differ: '+repr(sorted(paths)))
    sockets=set()
    for fd in (proc/'fd').iterdir():
        try:target=os.readlink(fd)
        except FileNotFoundError:continue
        match=re.fullmatch(r'socket:\[(\d+)\]',target)
        if match:sockets.add(match[1])
    udp=[]
    for table in ('udp','udp6'):
        for line in (proc/'net'/table).read_text().splitlines()[1:]:
            fields=line.split()
            if fields[9] in sockets:udp.append(line)
    if udp:raise ValueError('recorder owns UDP')
    return {'pid':pid,'start':process_start(pid),'hashes':{p:hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in sorted(paths)},'owned_udp':udp}


def inspect_bag(root,run,board,nonce,storage,source_role,source_board):
    import rosbag2_py
    from rclpy.serialization import deserialize_message
    from std_msgs.msg import String
    bag=root/'bags'/storage
    reader=rosbag2_py.SequentialReader();reader.open(rosbag2_py.StorageOptions(uri=str(bag),storage_id=storage),rosbag2_py.ConverterOptions('',''))
    types=sorted([{'name':m.name,'type':m.type,'format':m.serialization_format} for m in reader.get_all_topics_and_types()],key=lambda m:m['name'])
    records=[]
    while reader.has_next():
        name,data,stamp=reader.read_next();records.append({'topic':name,'timestamp':stamp,'payload':deserialize_message(data,String).data})
        if len(records)>10:raise ValueError('unexpected extra recorded messages')
    del reader
    wanted_types=sorted([{'name':topic(run,source_role,storage,k),'type':'std_msgs/msg/String','format':'cdr'} for k in ('main','noise')],key=lambda m:m['name'])
    if types!=wanted_types or len(records)!=10:raise ValueError('recorded topic/count mismatch')
    for kind in ('main','noise'):
        if [r['payload'] for r in records if r['topic']==topic(run,source_role,storage,kind)]!=payloads(run,nonce,source_board,storage,kind):raise ValueError('recorded samples differ from peer fixture')
    metadata=yaml.safe_load((bag/'metadata.yaml').read_text())['rosbag2_bagfile_information']
    value={'run_id':run,'nonce':nonce,'board':board,'storage':storage,'types':types,'records':records,'metadata':metadata}
    (root/('bag_'+storage+'_inspection.json')).write_text(json.dumps(value)+'\n')
    return value


def execute(argv,output,run,board,case,label,expected,nonce):
    root=output.parent;storage=expected['storage'];native=None;stop=None;emergency=False
    actual=[sys.executable,'-u','-B','-c','from ros2cli.cli import main; raise SystemExit(main())']+argv[1:]
    outpath=output/(label+'.stdout');errpath=output/(label+'.stderr')
    with outpath.open('wb') as out,errpath.open('wb') as err,subprocess.Popen(actual,stdout=out,stderr=err,start_new_session=True) as child:
        start=process_start(child.pid)
        def interrupt(sig,frame):
            if child.poll() is None:os.killpg(child.pid,signal.SIGKILL)
            child.wait();raise SystemExit(128+sig)
        old={s:signal.signal(s,interrupt) for s in (signal.SIGINT,signal.SIGTERM)}
        try:
            (root/('bag_'+storage+'.go')).write_text(nonce+'\n')
            deadline=time.monotonic()+20
            while time.monotonic()<deadline and child.poll() is None:
                if (root/('bag_'+storage+'_received.json')).exists():
                    native=inspect_process(child.pid,root,storage);time.sleep(2)
                    if process_start(child.pid)!=start:raise ValueError('recorder PID reused')
                    stop={'signal':15,'pid':child.pid,'start':start,'nonce':nonce};child.send_signal(signal.SIGTERM);break
                time.sleep(.1)
            try:child.wait(timeout=8)
            except subprocess.TimeoutExpired:emergency=True;os.killpg(child.pid,signal.SIGKILL);child.wait()
        finally:
            if child.poll() is None:emergency=True;os.killpg(child.pid,signal.SIGKILL);child.wait()
            for sig,handler in old.items():signal.signal(sig,handler)
    stdout=outpath.read_text();stderr=errpath.read_text();passed=child.returncode==0 and stop is not None and native is not None and not emergency
    detail={'native':native,'stop':stop,'emergency_cleanup':emergency}
    if passed:
        source_board=next(b for b in a.TARGET['board_serials'] if b!=board)
        inspect_bag(root,run,board,nonce,storage,expected['source_role'],source_board)
    execution={'argv':argv,'actual_argv':actual,'board_serial':board,'child_pid':child.pid,'child_start':start,'returncode':child.returncode,'bag_record':detail}
    raw='MDDS_CLI_ACTUAL_ARGV '+json.dumps(actual)+'\nMDDS_CLI_STDOUT_BEGIN\n'+stdout+'\nMDDS_CLI_STDOUT_END\nMDDS_CLI_STDERR_BEGIN\n'+stderr+'\nMDDS_CLI_STDERR_END\nMDDS_BAG_RECORD_PROCESS '+json.dumps(detail)+'\n'+a.terminal_marker(run,case,child.returncode,argv,board)+'\n'
    if passed:raw+='MDDS_CLI_FUNCTIONAL CASE='+case+' RESULT=PASS\n'
    log=output/(label+'.log');log.write_text(raw);execution['log']={'path':log.name,'sha256':a.digest(log.read_bytes())}
    return {'case_id':case,'label':label,'expected':expected,'passed':passed,'execution':execution}


def freeze_files(root):
    files=[]
    for path in sorted((root/'bags').rglob('*')):
        if path.is_dir():continue
        if path.is_symlink() or path.stat().st_size>16*1024*1024:raise ValueError('unexpected bag output')
        files.append({'path':path.relative_to(root).as_posix(),'sha256':a.digest(path.read_bytes()),'size':path.stat().st_size})
    (root/'bag_files.json').write_text(json.dumps(files,indent=2)+'\n')
    return files
