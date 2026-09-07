"""Explicit visible/hidden and native graph-name sets."""
import json
import re

NODES=('visible','_hidden')
def scope(run):return '/hidden_graph_'+run
def hidden(name):return any(v.startswith('_') for v in name.split('/'))
def groups(node):return ('visible','_secret') if node=='visible' else ('_hidden',)

def node_sets(run,role,node):
    peer='B' if role=='A' else 'A';ns=scope(run)+'/'+role
    result={'publishers':{'/parameter_events':['rcl_interfaces/msg/ParameterEvent']},'subscriptions':{},
            'services':{ns+'/'+node+'/get_type_description':['type_description_interfaces/srv/GetTypeDescription']},'clients':{},'actions':{}}
    for group in groups(node):
        base=ns+'/'+group;target=scope(run)+'/'+peer+'/'+group
        result['publishers'][base+'/out']=['std_msgs/msg/String'];result['subscriptions'][target+'/out']=['std_msgs/msg/String']
        result['services'][base+'/serve']=['example_interfaces/srv/AddTwoInts'];result['clients'][target+'/serve']=['example_interfaces/srv/AddTwoInts']
        result['actions'][base+'/action']=['example_interfaces/action/Fibonacci']
        for suffix,type_ in (('feedback','example_interfaces/action/Fibonacci_FeedbackMessage'),('status','action_msgs/msg/GoalStatusArray')):
            result['publishers'][base+'/action/_action/'+suffix]=[type_];result['subscriptions'][target+'/action/_action/'+suffix]=[type_]
        for suffix,type_ in (('send_goal','example_interfaces/action/Fibonacci_SendGoal'),('get_result','example_interfaces/action/Fibonacci_GetResult'),('cancel_goal','action_msgs/srv/CancelGoal')):
            result['services'][base+'/action/_action/'+suffix]=[type_];result['clients'][target+'/action/_action/'+suffix]=[type_]
    return result

def views(run,kind,include_hidden):
    result={}
    for role in ('A','B'):
        for node in NODES:
            data=node_sets(run,role,node)
            if kind=='node':result[scope(run)+'/'+role+'/'+node]=[]
            elif kind=='topic':result.update(data['publishers']);result.update(data['subscriptions'])
            elif kind=='service':result.update(data['services']);result.update(data['clients'])
            else:result.update(data['actions'])
    return {n:t for n,t in result.items() if n.startswith(scope(run)+'/') and (include_hidden or not hidden(n))}

def raw_by_node(run,role,node):
    data=node_sets(run,role,node);result={'publishers':{'rt'+n:t for n,t in data['publishers'].items()},'subscriptions':{'rt'+n:t for n,t in data['subscriptions'].items()}}
    for kind in ('services','clients'):
        for name,types in data[kind].items():
            result['publishers']['rr'+name+'Reply' if kind=='services' else 'rq'+name+'Request']=[types[0]+('_Response' if kind=='services' else '_Request')]
            result['subscriptions']['rq'+name+'Request' if kind=='services' else 'rr'+name+'Reply']=[types[0]+('_Request' if kind=='services' else '_Response')]
    return result

def native_topics(run):
    result={}
    for role in ('A','B'):
        for node in NODES:
            for rows in raw_by_node(run,role,node).values():result.update(rows)
    return {n:t for n,t in result.items() if n[2:].startswith(scope(run)+'/')}

def validate_snapshot(value,run):
    expected={'nodes':views(run,'node',True),'topics':views(run,'topic',True),'native_topics':native_topics(run),
              'native_by_node':{scope(run)+'/'+r+'/'+n:raw_by_node(run,r,n) for r in ('A','B') for n in NODES}}
    if json.dumps(value,sort_keys=True)!=json.dumps(expected,sort_keys=True):raise ValueError('hidden/native graph snapshot differs')

def parse_view(raw,run,kind,include_hidden):
    result={};seen=set()
    for line in raw.splitlines():
        if not line.strip():continue
        if kind=='node':name=line.strip();types=[]
        else:
            match=re.fullmatch(r'(/\S+) \[([^\[\]]+)\]',line.strip())
            if not match:raise ValueError('malformed typed CLI graph view')
            name,types=match[1],match[2].split(', ')
        if not name.startswith('/'):raise ValueError('invalid CLI graph name')
        # Duplicate baseline nodes are intentional; scoped fixture names are unique.
        if name.startswith(scope(run)+'/'):
            if name in seen:raise ValueError('duplicate hidden fixture entry')
            seen.add(name);result[name]=types
        if not include_hidden and hidden(name):raise ValueError('hidden entity leaked into default CLI view')
    if result!=views(run,kind,include_hidden):raise ValueError('CLI visible/hidden set differs')
    return result
