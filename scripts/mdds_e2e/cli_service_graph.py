"""Service graph CLI contract for the two-board ROS fixture."""


def recipe(namespace, peer_role):
    service=namespace+'/'+peer_role+'/alpha/serve'
    visible=sorted(namespace+'/'+role+'/'+name+'/serve' for role in ('A','B') for name in ('alpha','beta'))
    hidden=sorted(visible+[namespace+'/'+role+'/_hidden_service' for role in ('A','B')])
    info={'Type':'example_interfaces/srv/AddTwoInts','Clients count':'1','Services count':'1'}
    return [
        ('cli:service/type','service_type',['service','type',service],'example_interfaces/srv/AddTwoInts'),
        ('cli:service/find','service_find_visible',['service','find','example_interfaces/srv/AddTwoInts'],visible),
        ('cli:service/find','service_find_hidden',['service','find','example_interfaces/srv/AddTwoInts','--include-hidden-services'],hidden),
        ('cli:service/find','service_find_count',['service','find','example_interfaces/srv/AddTwoInts','--include-hidden-services','--count-services'],6),
        ('cli:service/info','service_info_cached',['service','info',service],info),
        ('cli:service/info','service_info_direct',['service','info',service,'--no-daemon','--spin-time','3'],info),
    ]


def oracle(case, stdout, expected):
    lines=[line.strip() for line in stdout.splitlines() if line.strip()]
    if case=='cli:service/type':return lines==[expected]
    if case=='cli:service/find':
        return lines==[str(expected)] if isinstance(expected,int) else sorted(lines)==sorted(expected)
    if case=='cli:service/info':
        fields={}
        for line in lines:
            key,separator,value=line.partition(':')
            if not separator or key in fields:return False
            fields[key]=value.strip()
        return fields==expected
    return False
