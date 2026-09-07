"""Rich peer ownership and identity change across a process restart."""
import json
def scope(run):return '/peer_restart_'+run
def path(run,role):return scope(run)+'/'+role
def payloads(run,nonce,role,generation):return [f'{run}|{nonce}|{role}|peer|{generation}|{i}' for i in range(3)]
def specs(run,role):
    own=path(run,role);peer=path(run,'B' if role=='A' else 'A');out=[]
    for topic,type_ in [('/parameter_events','rcl_interfaces/msg/ParameterEvent'),(own+'/out','std_msgs/msg/String'),(own+'/action/_action/feedback','example_interfaces/action/Fibonacci_FeedbackMessage'),(own+'/action/_action/status','action_msgs/msg/GoalStatusArray')]:out.append(('rt'+topic,1,type_))
    for topic,type_ in [(peer+'/out','std_msgs/msg/String'),(peer+'/action/_action/feedback','example_interfaces/action/Fibonacci_FeedbackMessage'),(peer+'/action/_action/status','action_msgs/msg/GoalStatusArray')]:out.append(('rt'+topic,2,type_))
    def service(name,type_,server):
        out.append(('rq'+name+'Request',2 if server else 1,type_+'_Request'));out.append(('rr'+name+'Reply',1 if server else 2,type_+'_Response'))
    service(scope(run)+'/peer_'+role+'/get_type_description','type_description_interfaces/srv/GetTypeDescription',True)
    for root,server in ((own,True),(peer,False)):
        service(root+'/serve','example_interfaces/srv/AddTwoInts',server)
        for suffix,type_ in [('send_goal','example_interfaces/action/Fibonacci_SendGoal'),('get_result','example_interfaces/action/Fibonacci_GetResult'),('cancel_goal','action_msgs/srv/CancelGoal')]:service(root+'/action/_action/'+suffix,type_,server)
    return sorted(out)
def validate_snapshot(value,run,generation,roles):
    expected=sorted([['peer_'+r,scope(run),'/peer/'+r+'/g'+str(generation)] for r in roles])
    if value['nodes']!=expected:raise ValueError('peer node generation/multiplicity differs')
    all_gids=[]
    for role in roles:
        rows=[v for v in value['endpoints'] if v['node']=='peer_'+role]
        if sorted((v['topic'],v['kind'],v['type']) for v in rows)!=specs(run,role):raise ValueError('peer endpoint set differs')
        if any(v['namespace']!=scope(run) for v in rows):raise ValueError('peer namespace differs')
        gids=[tuple(v['gid']) for v in rows]
        if any(len(g)!=16 or not any(g) or any(type(x) is not int or not 0<=x<=255 for x in g) for g in gids) or len({g[:12] for g in gids})!=1:raise ValueError('invalid peer participant identity')
        all_gids+=gids
    if len(value['endpoints'])!=25*len(roles) or len(set(all_gids))!=len(all_gids) or len({g[:12] for g in all_gids})!=len(roles):raise ValueError('peer identity collision or extra endpoint')
    if value['catalog']!=sorted({v['topic'] for v in value['endpoints']}):raise ValueError('peer catalog differs')
def validate_recovery(before,after,run):
    validate_snapshot(before,run,1,('A','B'));validate_snapshot(after,run,2,('A','B'))
    if {tuple(v['gid']) for v in before['endpoints']} & {tuple(v['gid']) for v in after['endpoints']}:raise ValueError('old peer identity survived restart')
    def metadata(value):return sorted((json.dumps({k:v for k,v in e.items() if k!='gid'},sort_keys=True) for e in value['endpoints']))
    if metadata(before)!=metadata(after):raise ValueError('peer endpoint metadata changed during restart')
def validate_processes(first,second):
    if first.get('returncode')!=-9 or second.get('returncode')!=0:raise ValueError('wrong peer exits')
    for value in (first,second):
        if type(value.get('pid')) is not int or value['pid']<=1 or not isinstance(value.get('start'),str) or not value['start'].isdecimal():raise ValueError('missing peer process identity')
    if (first['pid'],first['start'])==(second['pid'],second['start']):raise ValueError('peer process did not restart')
