"""Full endpoint identity and ownership across an SDK-only outage."""
import copy
import json

def local_view(before,role):
    names={'alpha_'+role,'beta_'+role,'duplicate_'+role}
    endpoints=[copy.deepcopy(v) for v in before['endpoints'] if v['node'] in names]
    return {'nodes':[list(v) for v in before['nodes'] if v[0] in names],'endpoints':endpoints,'catalog':sorted({v['topic'] for v in endpoints})}

def validate_views(before,paused,restored,role):
    if json.dumps(paused,sort_keys=True)!=json.dumps(local_view(before,role),sort_keys=True):raise ValueError('outage graph did not preserve exactly the local endpoints')
    if json.dumps(restored,sort_keys=True)!=json.dumps(before,sort_keys=True):raise ValueError('restored endpoint graph differs from before outage')

def collect(node,namespace,reference=None):
    topics={n for n,t in node.get_topic_names_and_types(no_demangle=True)}
    known={tuple(v['gid']) for v in reference['endpoints']} if reference else set()
    queried=topics|({v['topic'] for v in reference['endpoints']} if reference else set())
    endpoints=[]
    for topic in sorted(queried):
        for kind,get in ((1,node.get_publishers_info_by_topic),(2,node.get_subscriptions_info_by_topic)):
            for e in get(topic,no_mangle=True):
                gid=list(e.endpoint_gid)
                if e.node_namespace!=namespace and tuple(gid) not in known:continue
                q=e.qos_profile
                endpoints.append({'node':e.node_name,'namespace':e.node_namespace,'topic':topic,'type':e.topic_type,'hash':str(e.topic_type_hash),'gid':gid,'kind':kind,
                                  'qos':{'history':int(q.history),'depth':q.depth,'reliability':int(q.reliability),'durability':int(q.durability),'deadline':q.deadline.nanoseconds,
                                         'lifespan':q.lifespan.nanoseconds,'liveliness':int(q.liveliness),'lease':q.liveliness_lease_duration.nanoseconds}})
    endpoints.sort(key=lambda v:tuple(v['gid']))
    scoped={t for t in topics if any(t.startswith(p+namespace+'/') for p in ('rt','rq','rr'))}
    scoped|={v['topic'] for v in endpoints if v['topic'] in topics}
    expected={v['topic'] for v in endpoints}
    if scoped!=expected:raise ValueError('catalog and verbose endpoint queries disagree')
    gids=[tuple(v['gid']) for v in endpoints]
    if len(gids)!=len(set(gids)):raise ValueError('duplicate endpoint identities in graph')
    return {'nodes':sorted(list(v) for v in node.get_node_names_and_namespaces_with_enclaves() if v[1]==namespace),'endpoints':endpoints,'catalog':sorted(scoped)}
