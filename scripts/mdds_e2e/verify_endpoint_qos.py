"""Match endpoint metadata and received samples to the opposite board's sends."""
import json
import cli_acceptance as a
from endpoint_qos_contract import MATRIX,topic,delivers,validate as validate_contract


def validate(value,root,run,board,nonce):
    role='A' if board==a.TARGET['board_serials'][0] else 'B'
    report=value['endpoint_qos'];type_hash=json.loads((root/'type_hashes.json').read_bytes())['hashes']['std_msgs/msg/String']
    validate_contract(report,run,nonce,role,type_hash)
    if json.loads((root/(board+'.endpoint_qos.json')).read_bytes())!=report:raise ValueError('QoS report differs')
    raw=(root/(board+'.ros.log')).read_text()
    if raw.splitlines().count('ENDPOINT_QOS_RESULT '+json.dumps(report))!=1:raise ValueError('QoS result lacks raw process evidence')
    peer=next(b for b in a.TARGET['board_serials'] if b!=board)
    peer_role='B' if role=='A' else 'A'
    for name,policy in (('reliability_bad','RELIABILITY'),('durability_bad','DURABILITY'),('deadline_bad','DEADLINE'),
                        ('liveliness_bad','LIVELINESS'),('liveliness_lease_bad','LIVELINESS'),('liveliness_infinite_bad','LIVELINESS'),
                        ('deadline_precision_bad','DEADLINE'),('liveliness_precision_bad','LIVELINESS')):
        for source_role in (role,peer_role):
            text="topic '"+topic(run,source_role,name)+"'"
            if not any(text in line and line.endswith('Last incompatible policy: '+policy) for line in raw.splitlines()):raise ValueError('incompatible QoS event policy missing or wrong')
    sent=json.loads((root/(peer+'.endpoint_qos.json')).read_bytes())
    for case in MATRIX:
        name=case['name'];wanted=sent['sent'][name] if delivers(name) else []
        if report['received'][name]!=wanted:raise ValueError('QoS receiver differs from actual peer publication')
        for message in wanted:
            marker='ENDPOINT_QOS_RX '+json.dumps({'role':role,'case':name,'payload':message})
            if raw.splitlines().count(marker)!=1:raise ValueError('QoS receiving callback absent or duplicated')
    for owner in a.TARGET['board_serials']:
        for name in ('ready','go','sent','observe_go'):
            if (root/(owner+'.endpoint_qos.'+name)).read_text().strip()!=nonce:raise ValueError('QoS barrier missing or stale')
