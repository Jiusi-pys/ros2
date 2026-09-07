"""Validate standalone diagnostic UDP without promoting it to MDDS evidence."""
import json
import cli_acceptance as a
from cli_multicast import received_packet


def validate(value,root,run,board,nonce):
    for serial in a.TARGET['board_serials']:
        for stage in ('ready','send'):
            if (root/(serial+'.multicast.'+stage)).read_text().strip()!=nonce:raise ValueError('multicast barrier differs')
    record=next(r for r in value['results'] if r['case_id']=='cli:multicast/receive');e=record['execution'];expected=record['expected'];detail=e['multicast_receiver']
    ready={'pid':e['child_pid'],'start':e['child_start'],'udp_local':'00000000:'+format(expected['port'],'04X'),'group':expected['group'],'device':expected['device']}
    if detail['ready']!=ready or detail['emergency_cleanup'] or detail['barrier_nonce']!=nonce:raise ValueError('multicast receiver readiness/exit differs')
    raw=a.read_artifact({**e['log'],'path':board+'.'+e['log']['path']},root).decode()
    stdout=raw.split('MDDS_CLI_STDOUT_BEGIN\n',1)[1].split('\nMDDS_CLI_STDOUT_END',1)[0]
    port=received_packet(stdout,expected['peer_ip'])
    if port is None or detail['peer_source_port']!=port or raw.splitlines().count('MDDS_MULTICAST_RECEIVER '+json.dumps(detail))!=1:raise ValueError('multicast peer packet differs')
