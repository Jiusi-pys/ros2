"""Read node ownership, endpoint counts and live writer identities."""
from abrupt_graph_contract import scope,base,node_id,validate_snapshot

def collect(n,run,live):
    from rclpy.action.graph import get_action_server_names_and_types_by_node,get_action_client_names_and_types_by_node
    ns=scope(run)
    value={'nodes':sorted(s.rstrip('/')+'/'+name for name,s in n.get_node_names_and_namespaces() if s==ns),
           'topics':{name:sorted(t) for name,t in n.get_topic_names_and_types() if name.startswith(ns+'/')},
           'services':{name:sorted(t) for name,t in n.get_service_names_and_types() if name.startswith(ns+'/')},
           'by_node':{},'counts':{},'gids':{},'parameter_event_owners':sorted(e.node_namespace.rstrip('/')+'/'+e.node_name for e in n.get_publishers_info_by_topic('/parameter_events') if e.node_namespace==ns)}
    for fqn in value['nodes']:
        space,name=fqn.rsplit('/',1)
        getters={'publishers':n.get_publisher_names_and_types_by_node,'subscriptions':n.get_subscriber_names_and_types_by_node,'services':n.get_service_names_and_types_by_node,'clients':n.get_client_names_and_types_by_node}
        v={k:{topic:sorted(types) for topic,types in get(name,space)} for k,get in getters.items()}
        v['action_servers']=dict(get_action_server_names_and_types_by_node(n,name,space));v['action_clients']=dict(get_action_client_names_and_types_by_node(n,name,space));value['by_node'][fqn]=v
    for role in ('A','B'):
        for kind in ('survivor','victim'):
            path=base(run,role,kind);key=role+':'+kind
            value['counts'][key]={'publishers':n.count_publishers(path+'/out'),'subscriptions':n.count_subscribers(path+'/out'),'servers':n.count_services(path+'/serve'),'clients':n.count_clients(path+'/serve')}
            infos=n.get_publishers_info_by_topic(path+'/out')
            if kind=='survivor' or live:
                if len(infos)!=1 or infos[0].node_namespace.rstrip('/')+'/'+infos[0].node_name!=node_id(run,role,kind):raise ValueError('abrupt publisher identity differs')
                value['gids'][key]=list(infos[0].endpoint_gid)
            elif infos:raise ValueError('dead victim publisher remains')
    validate_snapshot(value,run,live);return value

def empty(n,run):
    ns=scope(run)
    return {'nodes':[s+'/'+name for name,s in n.get_node_names_and_namespaces() if s==ns],
            'topics':{name:t for name,t in n.get_topic_names_and_types() if name.startswith(ns+'/')},
            'services':{name:t for name,t in n.get_service_names_and_types() if name.startswith(ns+'/')},
            'parameter_event_owners':[e.node_name for e in n.get_publishers_info_by_topic('/parameter_events') if e.node_namespace==ns]}
