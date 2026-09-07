"""Read exact by-node and verbose RMW endpoint data without inferring ownership."""
from late_graph_contract import NODES,space,endpoint_specs,validate_snapshot


def collect(node,run,role,hashes):
    from rclpy.action.graph import get_action_client_names_and_types_by_node,get_action_server_names_and_types_by_node
    ns=space(run,role);sections={};endpoints={}
    getters={'publishers':node.get_publisher_names_and_types_by_node,'subscriptions':node.get_subscriber_names_and_types_by_node,
             'services':node.get_service_names_and_types_by_node,'clients':node.get_client_names_and_types_by_node}
    for name in NODES:
        sections[name]={kind:dict(getter(name,ns)) for kind,getter in getters.items()}
        sections[name]['action_servers']=dict(get_action_server_names_and_types_by_node(node,name,ns))
        sections[name]['action_clients']=dict(get_action_client_names_and_types_by_node(node,name,ns))
        endpoints[name]={}
        for key,spec in endpoint_specs(run,role,name).items():
            getter=node.get_publishers_info_by_topic if spec['direction']==1 else node.get_subscriptions_info_by_topic
            matches=[v for v in getter(spec['topic'],no_mangle=spec['raw']) if v.node_name==name and v.node_namespace==ns]
            if len(matches)!=1:raise ValueError('late endpoint missing or duplicated: '+key)
            info=matches[0];qos=info.qos_profile
            endpoints[name][key]={'node':info.node_name,'namespace':info.node_namespace,'type':info.topic_type,
                'type_hash':str(info.topic_type_hash),'direction':int(info.endpoint_type),'gid':list(info.endpoint_gid),
                'qos':{'history':int(qos.history),'depth':qos.depth,'reliability':int(qos.reliability),'durability':int(qos.durability),
                       'deadline':qos.deadline.nanoseconds,'lifespan':qos.lifespan.nanoseconds,'liveliness':int(qos.liveliness),
                       'lease':qos.liveliness_lease_duration.nanoseconds}}
    value={'nodes':sorted(n for n,s in node.get_node_names_and_namespaces() if s==ns),'sections':sections,'endpoints':endpoints}
    validate_snapshot(value,run,role,hashes)
    return value
