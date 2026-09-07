"""Exact survivor graph after an owned victim process is killed."""
import json
BOUND_NS=5_000_000_000
def scope(run):return '/abrupt_graph_'+run
def base(run,role,kind):return scope(run)+'/'+role+'/'+kind
def node_id(run,role,kind):return scope(run)+'/'+kind+'_'+role
def messages(run,nonce,role,kind,stage):return ['|'.join((run,nonce,role,kind,str(stage),str(i))) for i in range(3)]

def section(run,role,kind):
    peer='B' if role=='A' else 'A';own=base(run,role,kind);other=base(run,peer,'survivor')
    result={'publishers':{'/parameter_events':['rcl_interfaces/msg/ParameterEvent'],own+'/out':['std_msgs/msg/String']},
            'subscriptions':{other+'/out':['std_msgs/msg/String']},
            'services':{node_id(run,role,kind)+'/get_type_description':['type_description_interfaces/srv/GetTypeDescription'],own+'/serve':['example_interfaces/srv/AddTwoInts']},
            'clients':{other+'/serve':['example_interfaces/srv/AddTwoInts']},'action_servers':{},'action_clients':{}}
    if kind=='survivor':
        victim=base(run,peer,'victim');result['subscriptions'][victim+'/out']=['std_msgs/msg/String'];result['clients'][victim+'/serve']=['example_interfaces/srv/AddTwoInts']
    else:
        other=base(run,peer,'victim')
        result['action_servers'][own+'/action']=['example_interfaces/action/Fibonacci'];result['action_clients'][other+'/action']=['example_interfaces/action/Fibonacci']
        for suffix,type_ in (('feedback','example_interfaces/action/Fibonacci_FeedbackMessage'),('status','action_msgs/msg/GoalStatusArray')):
            result['publishers'][own+'/action/_action/'+suffix]=[type_];result['subscriptions'][other+'/action/_action/'+suffix]=[type_]
        for suffix,type_ in (('send_goal','example_interfaces/action/Fibonacci_SendGoal'),('get_result','example_interfaces/action/Fibonacci_GetResult'),('cancel_goal','action_msgs/srv/CancelGoal')):
            result['services'][own+'/action/_action/'+suffix]=[type_];result['clients'][other+'/action/_action/'+suffix]=[type_]
    return result

def expected(run,live):
    kinds=('survivor','victim') if live else ('survivor',)
    views={node_id(run,r,k):section(run,r,k) for r in ('A','B') for k in kinds};topics={};services={};counts={}
    for view in views.values():
        for key in ('publishers','subscriptions'):topics.update({n:t for n,t in view[key].items() if n.startswith(scope(run)+'/')})
        for key in ('services','clients'):services.update(view[key])
    for role in ('A','B'):
        counts[role+':survivor']={'publishers':1,'subscriptions':2 if live else 1,'servers':1,'clients':2 if live else 1}
        counts[role+':victim']={'publishers':1 if live else 0,'subscriptions':1,'servers':1 if live else 0,'clients':1}
    return {'nodes':sorted(views),'topics':topics,'services':services,'by_node':views,'counts':counts,'parameter_event_owners':sorted(views)}

def validate_snapshot(value,run,live):
    if json.dumps({k:v for k,v in value.items() if k!='gids'},sort_keys=True)!=json.dumps(expected(run,live),sort_keys=True):raise ValueError('abrupt graph ownership/counts differ')

def validate_pair(before,after,run):
    for value,live in ((before,True),(after,False)):
        validate_snapshot(value,run,live);keys={r+':'+k for r in ('A','B') for k in (('survivor','victim') if live else ('survivor',))}
        if set(value['gids'])!=keys:raise ValueError('abrupt endpoint identities missing')
        for gid in value['gids'].values():
            if len(gid)!=16 or not any(gid) or any(type(x) is not int or not 0<=x<=255 for x in gid):raise ValueError('invalid abrupt GID')
        if len({tuple(g[:12]) for g in value['gids'].values()})!=len(keys):raise ValueError('victim and survivor participants are not independent')
    for key,gid in after['gids'].items():
        if gid!=before['gids'][key]:raise ValueError('surviving participant identity changed')

def validate_kill(value,armed_ns,removed_ns):
    if value.get('returncode')!=-9 or value.get('signal')!=9:raise ValueError('victim was not killed with SIGKILL')
    values=(armed_ns,value['started_ns'],value['completed_ns'],removed_ns)
    if any(type(v) is not int or v<=0 for v in values) or not armed_ns<=value['started_ns']<=value['completed_ns']<=removed_ns:raise ValueError('abrupt causal timing differs')
    if removed_ns-armed_ns>BOUND_NS:raise ValueError('graph withdrawal exceeded bound')

def write_json(root,name,value):
    p=root/(name+'.pending')
    with p.open('x') as stream:stream.write(json.dumps(value)+'\n')
    p.replace(root/name)
