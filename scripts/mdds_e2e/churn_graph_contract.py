"""Exact graph ownership and identity invariants for twenty churn rounds."""
import json
ROUNDS=20

def scope(run):return '/graph_churn_'+run
def base(run,role,kind):return scope(run)+'/'+role+'/'+kind
def node_id(run,role,kind):return scope(run)+'/survivor_'+role if kind=='survivor' else base(run,role,kind)
def payload(run,nonce,role,kind,index):return '|'.join((run,nonce,role,kind,str(index)))

def section(run,role,kind):
    peer='B' if role=='A' else 'A';own=base(run,role,kind);other=base(run,peer,kind)
    value={'publishers':{'/parameter_events':['rcl_interfaces/msg/ParameterEvent'],own+'/out':['std_msgs/msg/String']},
           'subscriptions':{other+'/out':['std_msgs/msg/String']},
           'services':{node_id(run,role,kind)+'/get_type_description':['type_description_interfaces/srv/GetTypeDescription'],own+'/serve':['example_interfaces/srv/AddTwoInts']},
           'clients':{other+'/serve':['example_interfaces/srv/AddTwoInts']},'action_servers':{},'action_clients':{}}
    if kind=='survivor':
        value['publishers'][own+'/control']=['std_msgs/msg/String'];value['subscriptions'][other+'/control']=['std_msgs/msg/String']
    else:
        value['action_servers'][own+'/action']=['example_interfaces/action/Fibonacci'];value['action_clients'][other+'/action']=['example_interfaces/action/Fibonacci']
        for suffix,type_ in (('feedback','example_interfaces/action/Fibonacci_FeedbackMessage'),('status','action_msgs/msg/GoalStatusArray')):
            value['publishers'][own+'/action/_action/'+suffix]=[type_];value['subscriptions'][other+'/action/_action/'+suffix]=[type_]
        for suffix,type_ in (('send_goal','example_interfaces/action/Fibonacci_SendGoal'),('get_result','example_interfaces/action/Fibonacci_GetResult'),('cancel_goal','action_msgs/srv/CancelGoal')):
            value['services'][own+'/action/_action/'+suffix]=[type_];value['clients'][other+'/action/_action/'+suffix]=[type_]
    return value

def expected(run,index):
    if type(index) is not int or not 0<=index<ROUNDS*2:raise ValueError('invalid churn phase')
    kinds=('survivor','transient') if index%2==0 else ('survivor',)
    by_node={node_id(run,r,k):section(run,r,k) for r in ('A','B') for k in kinds}
    topics={};services={}
    for view in by_node.values():
        for key in ('publishers','subscriptions'):topics.update({n:t for n,t in view[key].items() if n.startswith(scope(run)+'/')})
        for key in ('services','clients'):services.update(view[key])
    return {'index':index,'nodes':sorted(by_node),'topics':topics,'services':services,'by_node':by_node,'parameter_event_owners':sorted(by_node)}

def validate_snapshot(value,run,index):
    if json.dumps({k:v for k,v in value.items() if k!='gids'},sort_keys=True)!=json.dumps(expected(run,index),sort_keys=True):raise ValueError('churn stable graph differs')
    keys={r+':'+k for r in ('A','B') for k in (('survivor','transient') if index%2==0 else ('survivor',))}
    if set(value['gids'])!=keys:raise ValueError('churn endpoint identities incomplete')
    for gid in value['gids'].values():
        if len(gid)!=16 or not any(gid) or any(type(v) is not int or not 0<=v<=255 for v in gid):raise ValueError('invalid churn GID')
    if len({tuple(g[:12]) for g in value['gids'].values()})!=len(keys):raise ValueError('churn contexts share participant identities')

def validate_history(values,run):
    if len(values)!=ROUNDS*2:raise ValueError('churn phases missing')
    seen=set()
    for index,value in enumerate(values):
        validate_snapshot(value,run,index)
        for role in ('A','B'):
            if value['gids'][role+':survivor']!=values[0]['gids'][role+':survivor']:raise ValueError('survivor identity changed')
            if index%2==0:
                gid=tuple(value['gids'][role+':transient'])
                if gid in seen:raise ValueError('transient identity reused')
                seen.add(gid)
