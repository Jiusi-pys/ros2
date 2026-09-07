"""Independent expected ownership for two fully populated graph nodes."""
import json

NODES=('one','two')
def space(run,role):return '/ownership_'+run+'/'+role


def sections(run,role,name):
    peer='B' if role=='A' else 'A';base=space(run,role)+'/'+name;target=space(run,peer)+'/'+name
    action=base+'/action';other_action=space(run,peer)+'/'+('two' if name=='one' else 'one')+'/action'
    result={'publishers':{'/parameter_events':['rcl_interfaces/msg/ParameterEvent'],base+'/out':['std_msgs/msg/String']},
            'subscriptions':{target+'/out':['std_msgs/msg/String']},
            'services':{base+'/get_type_description':['type_description_interfaces/srv/GetTypeDescription'],base+'/serve':['example_interfaces/srv/AddTwoInts']},
            'clients':{target+'/serve':['example_interfaces/srv/AddTwoInts']},
            'action_servers':{action:['example_interfaces/action/Fibonacci']},
            'action_clients':{other_action:['example_interfaces/action/Fibonacci']}}
    for suffix,type_ in (('feedback','example_interfaces/action/Fibonacci_FeedbackMessage'),('status','action_msgs/msg/GoalStatusArray')):
        result['publishers'][action+'/_action/'+suffix]=[type_]
        result['subscriptions'][other_action+'/_action/'+suffix]=[type_]
    for suffix,type_ in (('send_goal','example_interfaces/action/Fibonacci_SendGoal'),('get_result','example_interfaces/action/Fibonacci_GetResult'),('cancel_goal','action_msgs/srv/CancelGoal')):
        result['services'][action+'/_action/'+suffix]=[type_]
        result['clients'][other_action+'/_action/'+suffix]=[type_]
    return result


def validate_sections(value,run,role):
    expected={name:sections(run,role,name) for name in NODES}
    if json.dumps(value,sort_keys=True)!=json.dumps(expected,sort_keys=True):raise ValueError('late graph node ownership differs')


def endpoint_specs(run,role,name):
    result={};view=sections(run,role,name)
    for kind in ('publishers','subscriptions'):
        for topic,types in view[kind].items():result[kind+':'+topic]={'topic':topic,'type':types[0],'direction':1 if kind=='publishers' else 2,'raw':False,'custom':topic!='/parameter_events'}
    for kind in ('services','clients'):
        for service,types in view[kind].items():
            for suffix,wire,direction in (('_Request','rq'+service+'Request',2 if kind=='services' else 1),('_Response','rr'+service+'Reply',1 if kind=='services' else 2)):
                result[kind+':'+wire]={'topic':wire,'type':types[0]+suffix,'direction':direction,'raw':True,'custom':not service.endswith('/get_type_description')}
    return result


def validate_snapshot(value,run,role,hashes):
    validate_sections(value['sections'],run,role)
    if sorted(value['nodes'])!=list(NODES):raise ValueError('late graph node multiplicity differs')
    if set(value['endpoints'])!=set(NODES):raise ValueError('late graph endpoint owners incomplete')
    gids=[]
    for name in NODES:
        specs=endpoint_specs(run,role,name);records=value['endpoints'][name]
        if set(records)!=set(specs):raise ValueError('late graph endpoint set differs')
        for key,spec in specs.items():
            item=records[key];gid=item.get('gid',[])
            if any(item.get(k)!=v for k,v in {'node':name,'namespace':space(run,role),'type':spec['type'],'direction':spec['direction'],'type_hash':hashes[spec['type']]}.items()):raise ValueError('late endpoint metadata differs')
            if len(gid)!=16 or not any(gid) or any(type(v) is not int or not 0<=v<=255 for v in gid):raise ValueError('late endpoint GID invalid')
            gids.append(tuple(gid))
            if spec['custom'] and any(item['qos'].get(k)!=v for k,v in {'history':1,'depth':7 if name=='one' else 11,'reliability':1,'durability':2}.items()):raise ValueError('declared endpoint QoS differs')
    if len(gids)!=len(set(gids)):raise ValueError('endpoint GIDs duplicated')
    if len({gid[:12] for gid in gids})!=1:raise ValueError('source nodes do not share one participant prefix')
