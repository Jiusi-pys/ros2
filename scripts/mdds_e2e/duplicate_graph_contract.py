"""Exact name multiplicity, owner unions and stable participant identity."""
from collections import Counter


def namespace(run):return '/duplicate_graph_'+run
def topic(run,key):return namespace(run)+'/'+key


def validate(value,run,phase,initial=None):
    keys=[r+str(i) for r in ('A','B') for i in ((0,1) if phase==1 else (0,))]
    if value['phase']!=phase or Counter(map(tuple,value['nodes']))!=Counter({('same',namespace(run)):len(keys)}):
        raise ValueError('same-name node multiplicity differs')
    topics={topic(run,k):['std_msgs/msg/String'] for k in keys}
    services={topic(run,k)+'/serve':['example_interfaces/srv/AddTwoInts'] for k in keys}
    owned_services={**services,namespace(run)+'/same/get_type_description':['type_description_interfaces/srv/GetTypeDescription']}
    for kind,expected in [('publishers',topics),('subscriptions',topics),('services',owned_services),('clients',services)]:
        if value[kind]!=expected:raise ValueError('same-name owner union differs: '+kind)
    gids=value['gids']
    if set(gids)!=set(keys) or any(len(g)!=16 or any(type(v) is not int or not 0<=v<=255 for v in g) for g in gids.values()):
        raise ValueError('endpoint identities missing')
    if len({tuple(g[:12]) for g in gids.values()})!=len(keys):raise ValueError('participants are not independent')
    if initial is not None and any(g!=initial['gids'][k] for k,g in gids.items()):raise ValueError('survivor identity changed')


def payload(run,nonce,key,phase,index):return f'{run}|{nonce}|{key}|{phase}|{index}'
