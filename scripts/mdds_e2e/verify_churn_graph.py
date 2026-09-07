"""Bind all churn phases to peer control/data and native graph observations."""
import json
from churn_graph_contract import ROUNDS,validate_history,payload
import cli_acceptance as a

def validate(value,root,run,board,nonce):
    role='A' if board==a.TARGET['board_serials'][0] else 'B';peer='B' if role=='A' else 'A'
    report=value['churn_graph'];raw=(root/(board+'.ros.log')).read_text().splitlines()
    if any(report.get(k)!=v for k,v in {'run_id':run,'nonce':nonce,'role':role,'rounds':ROUNDS}.items()):raise ValueError('churn report identity differs')
    if json.loads((root/(board+'.churn_graph.json')).read_bytes())!=report:raise ValueError('churn source report differs')
    if raw.count('CHURN_GRAPH_RESULT '+json.dumps(report))!=1:raise ValueError('churn final report lacks native evidence')
    if type(report['query_attempts']) is not int or report['query_attempts']<ROUNDS*2:raise ValueError('churn query observations incomplete')
    validate_history([v['snapshot'] for v in report['phases']],run)
    if report['final']!={'nodes':[],'topics':{},'services':{},'parameter_event_owners':[]}:raise ValueError('churn final graph is not empty')
    if len(report['advances'])!=ROUNDS*2:raise ValueError('churn peer phase witnesses missing')
    peer_board=next(b for b in a.TARGET['board_serials'] if b!=board)
    peer_report=json.loads((root/(peer_board+'.churn_graph.json')).read_bytes())
    for index,(record,advance) in enumerate(zip(report['phases'],report['advances'])):
        if record['index']!=index or advance['index']!=index or advance['peer_ready'] not in (index,index+1):raise ValueError('churn phase sequence differs')
        if json.dumps(record['snapshot'],sort_keys=True)!=json.dumps(peer_report['phases'][index]['snapshot'],sort_keys=True):raise ValueError('churn peer graph identities disagree')
        if raw.count('CHURN_PHASE '+json.dumps(record))!=1 or raw.count('CHURN_ADVANCE '+json.dumps(advance))!=1:raise ValueError('churn native phase or advance missing')
        control={'run_id':run,'nonce':nonce,'role':peer,'index':index}
        if raw.count('CHURN_CONTROL_RX '+json.dumps(control))!=1:raise ValueError('churn phase lacks exact peer control callback')
        kinds=('survivor','transient') if index%2==0 else ('survivor',)
        if set(record['data'])!=set(kinds):raise ValueError('churn data owners missing')
        for kind in kinds:
            message=payload(run,nonce,peer,kind,index);seed=int(nonce[:7],16)
            if record['data'][kind]!={'received':message,'sum':seed+(1 if role=='A' else 2)+1000+index}:raise ValueError('churn peer data or RPC differs')
            rx={'role':role,'kind':kind,'index':index,'payload':message}
            request={'role':role,'kind':kind,'index':index,'a':seed+(1 if peer=='A' else 2),'b':1000+index,'sum':seed+(1 if peer=='A' else 2)+1000+index}
            if raw.count('CHURN_RX '+json.dumps(rx))!=1 or raw.count('CHURN_SERVICE_RX '+json.dumps(request))!=1:raise ValueError('churn data/RPC callback missing or duplicated')
    for name in ('hidden_source.go','hidden_cli.ready','hidden_cli.go','hidden_cli.done','hidden_source.stop','hidden_source.done'):
        if (root/(board+'.'+name)).read_text().strip()!=nonce:raise ValueError('churn lifecycle barrier differs')
    summary={'run_id':run,'nonce':nonce,'role':role,'phases':ROUNDS*2}
    if json.loads((root/(board+'.hidden_source.json')).read_bytes())!=summary or raw.count('CHURN_WORK_DONE '+json.dumps(summary))!=1:raise ValueError('churn completion summary differs')
