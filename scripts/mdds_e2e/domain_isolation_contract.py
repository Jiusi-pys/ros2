"""Exact domain-tagged data and same-name endpoint ownership."""
from collections import Counter
DOMAINS=(175,176)
def scope(run):return '/domain_isolation_'+run
def payloads(run,nonce,domain,role):return [f'{run}|{nonce}|d{domain}|{role}|{i}' for i in range(3)]
def specs(run,role):
    ns=scope(run);node='domain_'+role;peer='B' if role=='A' else 'A'
    values=[(node,'rt/parameter_events',1,'rcl_interfaces/msg/ParameterEvent'),(node,'rt'+ns+'/samples',1,'std_msgs/msg/String'),(node,'rt'+ns+'/samples',2,'std_msgs/msg/String')]
    for name,type_,server in [(ns+'/'+node+'/get_type_description','type_description_interfaces/srv/GetTypeDescription',True),(ns+'/'+role+'/serve','example_interfaces/srv/AddTwoInts',True),(ns+'/'+peer+'/serve','example_interfaces/srv/AddTwoInts',False)]:
        values.append((node,'rq'+name+'Request',2 if server else 1,type_+'_Request'));values.append((node,'rr'+name+'Reply',1 if server else 2,type_+'_Response'))
    return sorted(values)
def validate_snapshot(value,run,domain,hashes):
    if value['nodes']!=[['domain_'+r,scope(run),f'/isolation/d{domain}/'+r] for r in ('A','B')]:raise ValueError('foreign/missing domain nodes')
    rows=value['endpoints']
    if sorted((v['node'],v['topic'],v['kind'],v['type']) for v in rows)!=sorted(specs(run,'A')+specs(run,'B')):raise ValueError('domain endpoint set differs')
    if any(v['namespace']!=scope(run) or v['hash']!=hashes[v['type']] for v in rows):raise ValueError('domain endpoint metadata differs')
    gids=[tuple(v['gid']) for v in rows]
    if len(set(gids))!=18 or len({g[:12] for g in gids})!=2 or any(len(g)!=16 or any(type(v) is not int or not 0<=v<=255 for v in g) for g in gids):raise ValueError('domain endpoint identities differ')
    if value['catalog']!=sorted({v['topic'] for v in rows}):raise ValueError('domain catalog differs')
def validate_counts(value):
    if value!={'publishers':2,'subscriptions':2,'writer_matches':2,'reader_matches':2,'servers':1,'clients':1}:raise ValueError('foreign or absent domain matches')
def validate_data(value,run,nonce,domain):
    if Counter(value)!=Counter(payloads(run,nonce,domain,'A')+payloads(run,nonce,domain,'B')):raise ValueError('foreign or missing domain samples')
