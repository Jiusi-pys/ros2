"""Bind default/include-hidden CLI views to complete native graph evidence."""
import json
import cli_acceptance as a
from hidden_graph_contract import validate_snapshot,parse_view
from cli_hidden_graph import recipes,CASE


def validate(value,root,run,board,nonce):
    if any(value.get(k)!=v for k,v in {'run_id':run,'nonce':nonce,'board':board}.items()):raise ValueError('hidden graph identity differs')
    if json.loads((root/(board+'.hidden_cli.json')).read_bytes())!=value:raise ValueError('hidden CLI report differs')
    role='A' if board==a.TARGET['board_serials'][0] else 'B'
    source=json.loads((root/(board+'.hidden_source.json')).read_bytes())
    if any(source.get(k)!=v for k,v in {'run_id':run,'nonce':nonce,'role':role}.items()):raise ValueError('hidden source identity differs')
    validate_snapshot(source['snapshot'],run)
    if (root/(board+'.ros.log')).read_text().splitlines().count('HIDDEN_GRAPH_SOURCE '+json.dumps(source))!=1:raise ValueError('native hidden graph snapshot lacks raw evidence')
    for name in ('hidden_source.go','hidden_source.stop','hidden_source.done','hidden_cli.ready','hidden_cli.go','hidden_cli.done'):
        if (root/(board+'.'+name)).read_text().strip()!=nonce:raise ValueError('hidden graph barrier differs')
    if len(value['results'])!=len(recipes()):raise ValueError('hidden CLI views incomplete')
    for result,(kind,hidden,args) in zip(value['results'],recipes()):
        e=result['execution'];argv=['ros2']+args
        if result['kind']!=kind or result['hidden'] is not hidden or e['argv']!=argv or e['board_serial']!=board or e['returncode']!=0:raise ValueError('hidden CLI recipe/exit differs')
        if type(e['child_pid']) is not int or e['child_pid']<=0 or not e['child_start'].isdecimal():raise ValueError('hidden CLI process identity missing')
        raw=a.read_artifact({**e['log'],'path':board+'.'+e['log']['path']},root).decode()
        begin='MDDS_CLI_STDOUT_BEGIN\n';end='\nMDDS_CLI_STDOUT_END'
        if raw.count(begin)!=1 or raw.count(end)!=1:raise ValueError('hidden output capture malformed')
        rows=parse_view(raw.split(begin)[1].split(end)[0],run,kind,hidden)
        if result['rows']!=rows or raw.splitlines().count('HIDDEN_GRAPH_VIEW '+json.dumps({'kind':kind,'hidden':hidden,'rows':rows}))!=1:raise ValueError('hidden output observation differs')
        actual=['/data/python312-rk3588a/usr/bin/python3.12','-B','-c','from ros2cli.cli import main; raise SystemExit(main())']+args
        if e['actual_argv']!=actual or raw.splitlines().count('MDDS_GRAPH_ACTUAL_ARGV '+json.dumps(actual))!=1:raise ValueError('hidden CLI actual argv differs')
        if raw.splitlines().count(a.terminal_marker(run,CASE,0,argv,board))!=1:raise ValueError('hidden CLI terminal missing')


def emit(root,manifest,reports,run,nonce):
    case=next(c for c in manifest['cases'] if c['id']==CASE);executions=[]
    for board in a.TARGET['board_serials']:
        value=reports[board]['hidden_graph'];validate(value,root,run,board,nonce)
        for result in value['results']:
            e=result['execution'];executions.append({**e,'log':{**e['log'],'path':board+'.'+e['log']['path']}})
    first=reports[a.TARGET['board_serials'][0]]['hidden_graph']['results']
    receipt={'schema_version':1,'run_id':run,'case_id':CASE,'kind':'functional','status':'PASS','board_serials':a.TARGET['board_serials'],
             'rmw_implementation':'rmw_mdds','transport':'dsoftbus','executions':executions,
             'assertions':[{'id':'visible_set_exact' if index==0 else 'hidden_set_exact','passed':True,'execution':index,
                            'pattern':'HIDDEN_GRAPH_VIEW '+json.dumps({'kind':first[index]['kind'],'hidden':first[index]['hidden'],'rows':first[index]['rows']})} for index in (0,1)]}
    names=['host_report.json','ros2cli_overlay.json','ros2cli_overlay.zip']+[board+'.'+suffix for board in reports for suffix in ('hidden_source.json','hidden_cli.json','hidden_source.go','hidden_source.stop','hidden_source.done','hidden_cli.ready','hidden_cli.go','hidden_cli.done','daemon.log','daemon.inspect.json')]
    receipt['graph_provenance']=[{'path':name,'sha256':a.digest((root/name).read_bytes())} for name in names]
    path=root/'graph_hidden_entities.receipt.json';path.write_text(json.dumps(receipt,indent=2)+'\n');reference={'path':path.name,'sha256':a.digest(path.read_bytes())}
    a.validate_receipt(case,reference,manifest,root);case.update(status='PASS',evidence=[reference]);return CASE
