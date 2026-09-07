"""Independently check storage bytes, metadata and cross-board replay receipts."""
import json
import re
from decimal import Decimal
import yaml
import cli_acceptance as a
from bag_contract import FORMATS,topic,payloads,read_sqlite,read_mcap
from bag_file_list import paths


def local_file(root,board,path):return root/(board+'.'+path.replace('/','_'))


def validate(value,root,run,board,nonce):
    role='A' if board==a.TARGET['board_serials'][0] else 'B';peer='B' if role=='A' else 'A'
    peer_board=next(b for b in a.TARGET['board_serials'] if b!=board)
    files=json.loads((root/(board+'.bag_files.json')).read_text());paths(files)
    if value.get('bag_files')!=files:raise ValueError('bag manifest differs from batch record')
    for item in files:
        raw=local_file(root,board,item['path']).read_bytes()
        if len(raw)!=item['size'] or a.digest(raw)!=item['sha256']:raise ValueError('bag file hash/size differs')
    for storage in FORMATS:
        metadata=yaml.safe_load(local_file(root,board,'bags/'+storage+'/metadata.yaml').read_text())['rosbag2_bagfile_information']
        datafiles=metadata['relative_file_paths']
        if len(datafiles)!=1 or '/' in datafiles[0] or '\\' in datafiles[0]:raise ValueError('unexpected split bag path')
        datafile=local_file(root,board,'bags/'+storage+'/'+datafiles[0])
        types,messages=(read_sqlite if storage=='sqlite3' else read_mcap)(datafile)
        wanted_types=sorted((topic(run,peer,storage,k),'std_msgs/msg/String','cdr') for k in ('main','noise'))
        if sorted(types)!=wanted_types or len(messages)!=10:raise ValueError('bag storage topics/count differ')
        for kind in ('main','noise'):
            if [payload for name,stamp,payload in messages if name==topic(run,peer,storage,kind)]!=payloads(run,nonce,peer_board,storage,kind):raise ValueError('stored CDR samples differ')
        stamps=[stamp for _,stamp,_ in messages]
        if min(stamps)<=0 or max(stamps)<=min(stamps) or metadata['storage_identifier']!=storage or metadata['message_count']!=10 or metadata['duration']['nanoseconds']!=max(stamps)-min(stamps) or metadata['starting_time']['nanoseconds_since_epoch']!=min(stamps):raise ValueError('bag metadata time/count/storage differs')
        topic_rows=sorted((v['topic_metadata']['name'],v['topic_metadata']['type'],v['topic_metadata']['serialization_format'],v['message_count']) for v in metadata['topics_with_message_count'])
        if topic_rows!=sorted((*row,5) for row in wanted_types):raise ValueError('bag metadata topics differ')
        inspection=json.loads((root/(board+'.bag_'+storage+'_inspection.json')).read_text())
        if any(inspection.get(k)!=v for k,v in {'run_id':run,'nonce':nonce,'board':board,'storage':storage,'metadata':metadata}.items()):raise ValueError('native bag reader identity/metadata differs')
        native=sorted((r['topic'],r['timestamp'],r['payload']) for r in inspection['records'])
        if native!=sorted(messages):raise ValueError('native reader differs from independent storage reader')
        for observed_board,stage,field,expected_data in [
            (peer_board,'sent','sent',{k:payloads(run,nonce,peer_board,storage,k) for k in ('main','noise')}),
            (board,'received','received',{k:payloads(run,nonce,peer_board,storage,k) for k in ('main','noise')}),
            (peer_board,'played','received',payloads(run,nonce,peer_board,storage,'main'))]:
            proof=json.loads((root/(observed_board+'.bag_'+storage+'_'+stage+'.json')).read_text())
            wanted={'run_id':run,'nonce':nonce,'board':observed_board,'storage':storage,'stage':stage,field:expected_data}
            if proof!=wanted or (root/(observed_board+'.ros.log')).read_text().splitlines().count('CLI_BAG_PROOF '+json.dumps(proof))!=1:raise ValueError('bag source/receiver proof differs')
        record=next(r for r in value['results'] if r['label']=='bag_record_'+storage)
        execution=record['execution'];detail=execution['bag_record'];process=detail['native']
        remote='/data/local/tmp/ros2/.mdds-owned-runs/'+run
        wanted_hashes={remote+'/lib/'+name:a.digest((root/name).read_bytes()) for name in ('libmdds.so','librmw_mdds.so')}
        plugin='librosbag2_storage_'+storage+'.so';wanted_hashes['/data/local/tmp/ros2/Lib/'+plugin]=a.digest((root/plugin).read_bytes())
        if process['pid']!=execution['child_pid'] or process['start']!=execution['child_start'] or process['hashes']!=wanted_hashes or process['owned_udp'] or detail['emergency_cleanup'] or detail['stop']!={'signal':15,'pid':execution['child_pid'],'start':execution['child_start'],'nonce':nonce}:raise ValueError('recorder native process/stop proof differs')
        raw=a.read_artifact({**execution['log'],'path':board+'.'+execution['log']['path']},root).decode()
        if raw.splitlines().count('MDDS_BAG_RECORD_PROCESS '+json.dumps(detail))!=1:raise ValueError('recorder process log missing')
        info=next(r for r in value['results'] if r['label']=='bag_info_'+storage)
        raw=a.read_artifact({**info['execution']['log'],'path':board+'.'+info['execution']['log']['path']},root).decode()
        durations=re.findall(r'(?m)^Duration:\s*([0-9.]+)s',raw)
        if len(durations)!=1 or abs(Decimal(durations[0])*1000000000-metadata['duration']['nanoseconds'])>1000000:raise ValueError('CLI bag duration differs from stored timestamps')
