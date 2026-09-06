"""Build canonical dual-board CLI receipts only after exact functional checks."""
import copy
import json
from pathlib import Path
import sys
import cli_acceptance as acceptance
from cli_graph_basic import oracle

root=Path(sys.argv[1]);run=sys.argv[2];nonce=(root/'nonce').read_text().strip()
support=json.loads((root/'host_report.json').read_text())
if not support['passed']:raise ValueError('supporting production ROS/DSoftBus fixture failed')
manifest=json.loads((root/'cli_acceptance_manifest.json').read_text());manifest['run_id']=run
reports={}
contexts={}
for board in acceptance.TARGET['board_serials']:
    value=json.loads((root/(board+'.cli.results.json')).read_text())
    status=json.loads((root/(board+'.cli.status.json')).read_text())
    if value['run_id']!=run or value['board']!=board or value['nonce']!=nonce:raise ValueError('CLI batch identity mismatch')
    if status['run_id']!=run or status['role']!='cli' or status['returncode']!=0:raise ValueError('CLI batch child failed')
    record=(root/(board+'.cli.child.pid')).read_text().strip()
    if record!=f"MDDS_OWNED_PROCESS RUN_ID={run} TAG=cli_child PID={status['child_pid']} START={status['child_start']}":raise ValueError('CLI batch ownership mismatch')
    if value.get('daemon_absence') != [{'command_index':index,'domain':175,'daemons':[],'port_bindable':True} for index in range(-1,len(value['results']))]:raise ValueError('CLI daemon isolation not proven at every command boundary')
    reports[board]=value['results']
    contexts[board]=json.loads((root/(board+'.cli.fixture.json')).read_text())
    if contexts[board]['run_id']!=run or contexts[board]['nonce']!=nonce or contexts[board]['board']!=board:raise ValueError('fixture context mismatch')
passed=[]
for case in manifest['cases']:
    if case['id'] not in ('cli:topic/type','cli:topic/find','cli:service/call','cli:topic/info','cli:topic/pub','cli:topic/echo','cli:topic/list','cli:service/list'):continue
    listing=case['id'] in ('cli:topic/list','cli:service/list')
    selected=[]
    for board in acceptance.TARGET['board_serials']:
        board_results=[r for r in reports[board] if r['case_id']==case['id']]
        if len(board_results)!=(2 if listing else 1):raise ValueError('missing or duplicate CLI variant')
        if listing:
            option='--include-hidden-'+('topics' if case['id']=='cli:topic/list' else 'services')
            if sorted(option in r['execution']['argv'] for r in board_results)!=[False,True]:raise ValueError('missing visible/hidden CLI comparison')
        selected.extend((board,r) for r in board_results)
    executions=[]
    for board,result in selected:
        execution=copy.deepcopy(result['execution'])
        execution['log']['path']=board+'.'+execution['log']['path']
        raw=acceptance.read_artifact(execution['log'],root).decode('utf-8')
        stdout=raw.split('MDDS_CLI_STDOUT_BEGIN\n',1)[1].split('\nMDDS_CLI_STDOUT_END',1)[0]
        peer='B' if board==acceptance.TARGET['board_serials'][0] else 'A'
        namespace='/ros_broker_'+run
        base=int(nonce[:7],16)+(200 if peer=='B' else 100)
        expected={'cli:topic/type':'std_msgs/msg/String',
                  'cli:topic/find':sorted(namespace+'/'+role+'/'+name+'/out' for role in ('A','B') for name in ('alpha','beta')),
                  'cli:service/call':int(nonce[:7],16)+(1 if peer=='B' else 2)+17,
                  'cli:topic/info':contexts[board]['topics'][namespace+'/'+peer+'/alpha/out'],
                  'cli:topic/pub':base+2,'cli:topic/echo':base+1,
                  'cli:topic/list':{'namespace':namespace,'kind':'topic','hidden':'--include-hidden-topics' in execution['argv']},
                  'cli:service/list':{'namespace':namespace,'kind':'service','hidden':'--include-hidden-services' in execution['argv']}}[case['id']]
        if case['id']=='cli:topic/info':
            local_role='A' if peer=='B' else 'B'
            required={'Node namespace':namespace,'Topic type':'std_msgs/msg/String',
                      'Topic type hash':json.loads((root/'type_hashes.json').read_text())['hashes']['std_msgs/msg/String'],
                      'Reliability':'RELIABLE','History (Depth)':'KEEP_LAST (32)','Durability':'VOLATILE',
                      'Lifespan':'Infinite','Deadline':'Infinite','Liveliness':'AUTOMATIC','Liveliness lease duration':'Infinite'}
            for kind,owner in [('publisher',peer),('subscription',local_role)]:
                row=expected[kind]
                if row['Node name']!='alpha_'+owner or row['Endpoint type']!=kind.upper() or any(row.get(k)!=v for k,v in required.items()):raise ValueError('fixture endpoint fields violate requested contract')
                parts=row['GID'].split('.')
                if len(parts)!=16 or not any(int(v,16) for v in parts):raise ValueError('invalid fixture GID')
        if case['id']=='cli:topic/pub':
            peer_board=next(v for v in acceptance.TARGET['board_serials'] if v!=board)
            received=json.loads((root/(peer_board+'.cli.received.json')).read_text())
            if received!={'run_id':run,'nonce':nonce,'board':peer_board,'data':expected,'count':1}:raise ValueError('peer did not receive exact once-only CLI payload')
            peer_log=(root/(peer_board+'.ros.log')).read_text()
            marker='CLI_PUB_RX '+json.dumps(received)
            if peer_log.splitlines().count(marker)!=1:raise ValueError('peer callback evidence missing')
        if result['expected']!=expected or not result['passed'] or not oracle(case['id'],stdout,expected):raise ValueError('functional CLI oracle failed')
        if case['id'] in ('cli:topic/type','cli:topic/find','cli:topic/info','cli:topic/echo','cli:topic/list','cli:service/list') and '--no-daemon' not in execution['argv']:raise ValueError('CLI strategy command is not explicitly isolated')
        if 'mdds transports active: dsoftbus(local=AF_UNIX physical=dsoftbus_broker' not in raw:raise ValueError('CLI did not report production broker selection')
        if not str(execution['child_start']).isdecimal() or execution['child_pid']<=0:raise ValueError('missing actual CLI child identity')
        executions.append(execution)
    marker='MDDS_CLI_FUNCTIONAL CASE='+case['id']+' RESULT=PASS'
    receipt={'schema_version':1,'run_id':run,'case_id':case['id'],'kind':'functional','status':'PASS',
             'board_serials':acceptance.TARGET['board_serials'],'rmw_implementation':'rmw_mdds','transport':'dsoftbus',
             'executions':executions,'assertions':[{'id':'functional_result','passed':True,'execution':0,'pattern':marker}],
             'supporting_fixture':{'path':'host_report.json','sha256':acceptance.digest((root/'host_report.json').read_bytes())}}
    if case['id']=='cli:topic/pub':
        receipt['peer_callbacks']=[{'path':board+'.cli.received.json','sha256':acceptance.digest((root/(board+'.cli.received.json')).read_bytes())} for board in acceptance.TARGET['board_serials']]
    path=root/(case['id'].replace(':','_').replace('/','_')+'.receipt.json');path.write_text(json.dumps(receipt,indent=2)+'\n')
    ref={'path':path.name,'sha256':acceptance.digest(path.read_bytes())};acceptance.validate_receipt(case,ref,manifest,root)
    case['status']='PASS';case['evidence']=[ref];passed.append(case['id'])
(root/'cli_partial_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('CLI_GRAPH_BASIC_PASS '+json.dumps(passed))
