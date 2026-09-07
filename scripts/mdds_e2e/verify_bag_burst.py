"""Tie the real paused player's burst to exactly three callbacks on its peer."""
import json
import re
import yaml
import cli_acceptance as a
from bag_burst import check_proof,qos_options
from bag_contract import FORMATS


def validate(value,root,run,board,nonce):
    peer=next(b for b in a.TARGET['board_serials'] if b!=board)
    for storage in FORMATS:
        proof=json.loads((root/(peer+'.bag_'+storage+'_burst.json')).read_text())
        check_proof(proof,run,nonce,peer,storage)
        if (root/(peer+'.ros.log')).read_text().splitlines().count('CLI_BAG_PROOF '+json.dumps(proof))!=1:raise ValueError('burst callback not bound to peer process')
        if (root/(board+'.bag_'+storage+'.burst_stop')).read_text().strip()!=nonce:raise ValueError('burst stop barrier differs')
        record=next(r for r in value['results'] if r['label']=='bag_burst_'+storage)
        execution=record['execution'];detail=execution['burst_process'];native=detail['native']
        remote='/data/local/tmp/ros2/.mdds-owned-runs/'+run
        wanted={remote+'/lib/'+name:a.digest((root/name).read_bytes()) for name in ('libmdds.so','librmw_mdds.so')}
        plugin='librosbag2_storage_'+storage+'.so';wanted['/data/local/tmp/ros2/Lib/'+plugin]=a.digest((root/plugin).read_bytes())
        if native['pid']!=execution['child_pid'] or native['start']!=execution['child_start'] or native['hashes']!=wanted or native['owned_udp'] or detail['emergency_cleanup'] or detail['stop']!={'signal':15,'pid':execution['child_pid'],'start':execution['child_start'],'nonce':nonce}:raise ValueError('burst process/stop differs')
        options=root/(board+'.bag_burst_'+storage+'.yaml')
        if a.digest(options.read_bytes())!=detail['qos_sha256'] or yaml.safe_load(options.read_text())!=qos_options(record['expected']['source_topic']):raise ValueError('burst QoS configuration differs')
        raw=a.read_artifact({**execution['log'],'path':board+'.'+execution['log']['path']},root).decode()
        if re.findall(r'Burst ([0-9]+) messages\.',raw)!=['3'] or 'dsoftbus(local=AF_UNIX physical=dsoftbus_broker' not in raw:raise ValueError('player burst count or transport differs')
        if raw.splitlines().count('MDDS_BAG_BURST_PROCESS '+json.dumps(detail))!=1:raise ValueError('burst process observation missing')
