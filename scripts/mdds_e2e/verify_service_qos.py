"""Bind service availability to peer-owned graph and actual response callbacks."""
import json
import cli_acceptance as a
from service_qos_contract import calls,validate as validate_contract


def validate(value,root,run,board,nonce):
    role='A' if board==a.TARGET['board_serials'][0] else 'B'
    report=value['service_qos'];validate_contract(report,run,nonce,role)
    if json.loads((root/(board+'.service_qos.json')).read_bytes())!=report:raise ValueError('service QoS report differs')
    if (root/(board+'.ros.log')).read_text().splitlines().count('SERVICE_QOS_RESULT '+json.dumps(report))!=1:
        raise ValueError('service QoS result lacks actual process log')
    peer=next(b for b in a.TARGET['board_serials'] if b!=board)
    wanted={'run_id':run,'nonce':nonce,'role':'B' if role=='A' else 'A','served':sorted(calls(nonce,role),key=lambda v:v['kind'])}
    if json.loads((root/(peer+'.service_qos_server.json')).read_bytes())!=wanted:raise ValueError('peer service callbacks differ')
    if (root/(peer+'.ros.log')).read_text().splitlines().count('SERVICE_QOS_SERVER '+json.dumps(wanted))!=1:
        raise ValueError('peer service callbacks lack process log')
