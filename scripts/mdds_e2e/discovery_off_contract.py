"""Independent OFF/ON endpoint, match and payload expectations."""
from collections import Counter

def scope(run):return '/discovery_off_'+run
def payloads(run,nonce,role,mode):return [f'{run}|{nonce}|{role}|{mode}|{i}' for i in range(3)]
def expected_nodes(run,role,mode):
    ns=scope(run)
    return sorted([['off_'+role,ns,'/discovery/'+role+'/off'],['off_local_'+role,ns,'/discovery/'+role+'/off']] if mode=='off' else [['on_'+r,ns,'/discovery/'+r+'/on'] for r in ('A','B')])
def specs(run,role,mode):
    ns=scope(run);other='B' if role=='A' else 'A';result=[]
    def topic(node,name,kind,type_):result.append((node,name,kind,type_))
    def service(node,name,server):
        type_='type_description_interfaces/srv/GetTypeDescription' if name.endswith('/get_type_description') else 'example_interfaces/srv/AddTwoInts'
        topic(node,'rq'+name+'Request',2 if server else 1,type_+'_Request');topic(node,'rr'+name+'Reply',1 if server else 2,type_+'_Response')
    names=['off_'+role,'off_local_'+role] if mode=='off' else ['on_'+role]
    for name in names:
        topic(name,'rt/parameter_events',1,'rcl_interfaces/msg/ParameterEvent');service(name,ns+'/'+name+'/get_type_description',True)
    if mode=='off':
        topic(names[0],'rt'+ns+'/samples',1,'std_msgs/msg/String');topic(names[1],'rt'+ns+'/samples',2,'std_msgs/msg/String');service(names[0],ns+'/'+names[0]+'/serve',True)
        for name in ('off_'+role,'on_'+role,'on_'+other):service(names[1],ns+'/'+name+'/serve',False)
    else:
        for kind in (1,2):topic(names[0],'rt'+ns+'/samples',kind,'std_msgs/msg/String')
        service(names[0],ns+'/'+names[0]+'/serve',True)
        for name in ('on_'+other,'off_'+role,'off_'+other):service(names[0],ns+'/'+name+'/serve',False)
    return sorted(result)
def validate_snapshot(value,run,role,mode,hashes):
    if value['nodes']!=expected_nodes(run,role,mode):raise ValueError('OFF/ON node visibility differs')
    expected=specs(run,role,mode) if mode=='off' else specs(run,'A','on')+specs(run,'B','on')
    rows=value['endpoints']
    if sorted((v['node'],v['topic'],v['kind'],v['type']) for v in rows)!=sorted(expected):raise ValueError('OFF/ON endpoint visibility differs')
    if any(v['namespace']!=scope(run) or v['hash']!=hashes[v['type']] for v in rows):raise ValueError('OFF/ON endpoint metadata differs')
    gids=[tuple(v['gid']) for v in rows]
    if len(gids)!=len(set(gids)) or any(len(g)!=16 or not any(g) for g in gids):raise ValueError('OFF/ON endpoint identity invalid')
    if len({g[:12] for g in gids})!=(1 if mode=='off' else 2):raise ValueError('OFF/ON context identities differ')
    if value['catalog']!=sorted({v['topic'] for v in rows}):raise ValueError('OFF/ON topic catalog differs')
def validate_counts(value):
    expected={mode+'_'+key:n for mode,n in (('off',1),('on',2)) for key in ('publishers','subscriptions','writer_matches','reader_matches')}
    if value!=expected:raise ValueError('OFF/ON match counts differ')
def validate_data(value,run,nonce,role):
    if value['off']!=payloads(run,nonce,role,'off'):raise ValueError('OFF context received foreign/missing data')
    expected=payloads(run,nonce,'A','on')+payloads(run,nonce,'B','on')
    if Counter(value['on'])!=Counter(expected):raise ValueError('enabled control did not receive exact local and peer data')
