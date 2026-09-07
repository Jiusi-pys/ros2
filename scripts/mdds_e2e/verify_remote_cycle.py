"""Validate real driver generations around the unchanged ROS baseline."""
import hashlib
import json
from pathlib import Path
import re
import sys
from remote_cycle_contract import validate
from cli_acceptance import TARGET

def check(root):
    run=root.name;nonce=(root/'nonce').read_text().strip()
    if not json.loads((root/'host_report.json').read_bytes())['passed']:raise ValueError('ROS baseline did not pass')
    result={'run_id':run,'nonce':nonce,'passed':True,'scope':'real SDK pause/restore with local clients retained and ROS baseline recovery','full_reconnect_case':False,'boards':[]}
    for board in TARGET['board_serials']:
        status=json.loads((root/(board+'.daemon.status.json')).read_bytes());pid=status['child_pid']
        records={key:json.loads((root/(board+'.reconnect.'+suffix+'.json')).read_bytes()) for key,suffix in [('ready','ready'),('paused','paused'),('restored','restored'),('stop1','sdk_stop_1'),('stop2','sdk_stop_2')]}
        validate(records,run,nonce,pid);raw=(root/(board+'.daemon.log')).read_text();lines=raw.splitlines()
        for key,record in records.items():
            prefix='RECONNECT_SDK_STOP ' if key.startswith('stop') else 'RECONNECT_STAGE '
            matches=[json.loads(line[len(prefix):]) for line in lines if line.startswith(prefix)]
            if matches.count(record)!=1:raise ValueError('SDK phase lacks native log evidence')
        nonces=re.findall(r'^RECONNECT_RECEIVE_NONCE generation=(\d+) nonce=([0-9a-f]{32})$',raw,re.M)
        if {g for g,n in nonces}!={'1','2'} or len({n for g,n in nonces})!=len(nonces):raise ValueError('SDK receive identities were not fresh')
        if len(re.findall(r'^\[mdds/dsoftbus\] OnBind\(',raw,re.M))<2:raise ValueError('no second actual SDK bind')
        for name in ('reconnect.enabled','reconnect.pause','reconnect.resume'):
            if (root/(board+'.'+name)).read_text().strip()!=nonce:raise ValueError('SDK cycle command differs')
        result['boards'].append({'board':board,'pid':pid,'records':records,'receive_nonces':nonces})
    return result

if __name__=='__main__':
    root=Path(sys.argv[1]);value=check(root);p=root/'remote_cycle_report.json';p.write_text(json.dumps(value,indent=2)+'\n')
    print('REMOTE_CYCLE_PASS sha256='+hashlib.sha256(p.read_bytes()).hexdigest()+' full_reconnect_case=false')
