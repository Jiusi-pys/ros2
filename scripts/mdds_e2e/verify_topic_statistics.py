"""Validate CLI statistics against finite identified peer streams and clocks."""
import json
import cli_acceptance as a
from topic_statistics import VERBS,COUNT,payload,oracle


def validate(value,root,run,board,nonce):
    peer=next(b for b in a.TARGET['board_serials'] if b!=board)
    for verb in VERBS:
        received=json.loads((root/(board+'.stats_'+verb+'_received.json')).read_text())
        sent=json.loads((root/(peer+'.stats_'+verb+'_sent.json')).read_text())
        expected=[payload(run,nonce,peer,verb,i) for i in range(COUNT)]
        for record,source,stage in ((received,board,'received'),(sent,peer,'sent')):
            if any(record.get(k)!=v for k,v in {'run_id':run,'nonce':nonce,'board':source,'verb':verb,'stage':stage,'data':expected}.items()):raise ValueError('statistics stream identity/payload differs')
            if (root/(source+'.ros.log')).read_text().splitlines().count('CLI_STATS_PROOF '+json.dumps(record))!=1:raise ValueError('statistics proof not bound to source log')
        request={'run_id':run,'nonce':nonce,'verb':verb,'source':peer,'receiver':board,'stamp_ns':received['stamp_ns']}
        if received['request']!=request or sent['request']!=request or request['stamp_ns']<=0:raise ValueError('receiver-clock request differs')
        for times in (sent['publish_ns'],sent['monotonic_ns'],received['receive_ns']):
            if len(times)!=COUNT or any(type(t) is not int or t<=0 for t in times) or any(b<=a for a,b in zip(times,times[1:])):raise ValueError('statistics sample times differ')
        if not 2e9<=sent['monotonic_ns'][-1]-sent['monotonic_ns'][0]<=8e9:raise ValueError('statistics fixture did not maintain its bounded rate')
        sizes=[4+((13+len(v.encode())+7)//8)*8+24 if verb=='delay' else len(v.encode())+9 for v in expected]
        if received['sizes']!=sizes:raise ValueError('serialized fixture size differs')
        if (root/(board+'.stats_'+verb+'.go')).read_text().strip()!=nonce:raise ValueError('statistics start nonce differs')
        result=next(r for r in value['results'] if r['label']=='topic_'+verb);execution=result['execution'];detail=execution['statistics_process'];native=detail['native']
        remote='/data/local/tmp/ros2/.mdds-owned-runs/'+run;wanted={remote+'/lib/'+name:a.digest((root/name).read_bytes()) for name in ('libmdds.so','librmw_mdds.so')}
        if native['pid']!=execution['child_pid'] or native['start']!=execution['child_start'] or native['hashes']!=wanted or native['owned_udp'] or detail['emergency_cleanup']:raise ValueError('statistics native process differs')
        raw=a.read_artifact({**execution['log'],'path':board+'.'+execution['log']['path']},root).decode()
        stdout=raw.split('MDDS_CLI_STDOUT_BEGIN\n',1)[1].split('\nMDDS_CLI_STDOUT_END',1)[0]
        observed=detail['observed_stdout']
        if not isinstance(observed,str) or not observed or not stdout.startswith(observed) or not oracle(verb,observed,received) or not oracle(verb,stdout,received):raise ValueError('statistics output differs from observed stream')
        if 'dsoftbus(local=AF_UNIX physical=dsoftbus_broker' not in raw or raw.splitlines().count('MDDS_STATISTICS_PROCESS '+json.dumps(detail))!=1:raise ValueError('statistics process log differs')
