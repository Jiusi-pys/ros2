"""Exact node ownership, including action endpoints and hidden graph views."""
SECTIONS=('Subscribers','Publishers','Service Servers','Service Clients','Action Servers','Action Clients')


def contract(ns, role, name, hidden=False):
    other='B' if role=='A' else 'A'
    node=ns+'/'+name+'_'+role
    sections={key:{} for key in SECTIONS}
    sections['Publishers']['/parameter_events']='rcl_interfaces/msg/ParameterEvent'
    sections['Service Servers'][node+'/get_type_description']='type_description_interfaces/srv/GetTypeDescription'
    if name!='duplicate':
        base=ns+'/'+role+'/'+name;peer=ns+'/'+other+'/'+name
        sections['Publishers'][base+'/out']='std_msgs/msg/String'
        sections['Subscribers'][peer+'/out']='std_msgs/msg/String'
        sections['Service Servers'][base+'/serve']='example_interfaces/srv/AddTwoInts'
        sections['Service Clients'][peer+'/serve']='example_interfaces/srv/AddTwoInts'
        action=base+'/action';peer_action=ns+'/'+other+'/'+('beta' if name=='alpha' else 'alpha')+'/action'
        sections['Action Servers'][action]='example_interfaces/action/Fibonacci'
        sections['Action Clients'][peer_action]='example_interfaces/action/Fibonacci'
        if hidden:
            for suffix,type_name in [('feedback','example_interfaces/action/Fibonacci_FeedbackMessage'),('status','action_msgs/msg/GoalStatusArray')]:
                sections['Publishers'][action+'/_action/'+suffix]=type_name
                sections['Subscribers'][peer_action+'/_action/'+suffix]=type_name
            for suffix,type_name in [('send_goal','example_interfaces/action/Fibonacci_SendGoal'),('get_result','example_interfaces/action/Fibonacci_GetResult'),('cancel_goal','action_msgs/srv/CancelGoal')]:
                sections['Service Servers'][action+'/_action/'+suffix]=type_name
                sections['Service Clients'][peer_action+'/_action/'+suffix]=type_name
        if name=='alpha':
            sections['Publishers'][ns+'/'+role+'/cli_source']='std_msgs/msg/Int32'
            sections['Subscribers'][ns+'/'+role+'/cli_sink']='std_msgs/msg/Int32'
            if hidden:
                sections['Publishers'][ns+'/'+role+'/_hidden']='std_msgs/msg/Bool'
                sections['Subscribers'][ns+'/'+other+'/_hidden']='std_msgs/msg/Bool'
                sections['Service Servers'][ns+'/'+role+'/_hidden_service']='example_interfaces/srv/AddTwoInts'
                sections['Service Clients'][ns+'/'+other+'/_hidden_service']='example_interfaces/srv/AddTwoInts'
    return {'node':node,'sections':sections,'duplicate':name=='duplicate'}


def recipe(ns,role):
    cases=[]
    for name,hidden in [('alpha',False),('beta',False),('duplicate',False),('alpha',True)]:
        for direct in (False,True):
            expected=contract(ns,role,name,hidden)
            label='node_info_'+name+('_hidden' if hidden else '')+('_direct' if direct else '_cached')
            argv=['node','info',expected['node']]+(['--include-hidden'] if hidden else [])+(['--no-daemon','--spin-time','3'] if direct else [])
            cases.append(('cli:node/info',label,argv,expected))
    return cases


def oracle(stdout,expected):
    lines=[line.strip() for line in stdout.splitlines() if line.strip()]
    if not lines or lines[0]!=expected['node']:return False
    sections={};current=None
    for line in lines[1:]:
        if line.endswith(':') and line[:-1] in SECTIONS:
            current=line[:-1]
            if current in sections:return False
            sections[current]={}
        else:
            name,separator,type_name=line.partition(': ')
            if not separator or current is None or name in sections[current]:return False
            sections[current][name]=type_name
    return sections==expected['sections']
