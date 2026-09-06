"""Parameter read contracts covering all ROS scalar and array value types."""
import ast
import yaml


def values(nonce,role):
    count=int(nonce[:7],16)%50+10
    return {'flag':True,'count':count,'ratio':1.25,'text':'param_'+nonce+'_'+role,
            'octets':[0,127,255],'flags':[True,False,True],'counts':[-3,0,count],
            'ratios':[-1.5,0.0,2.25],'texts':['left_'+role,'right_'+nonce[:8]],
            'locked':42,'ephemeral':'delete_me','use_sim_time':False,'start_type_description_service':True}


def recipe(ns,peer,nonce):
    node=ns+'/alpha_'+peer;data=values(nonce,peer)
    labels={'flag':'Boolean value is:','count':'Integer value is:','ratio':'Double value is:','text':'String value is:',
            'octets':'Byte values are:','flags':'Boolean values are:','counts':'Integer values are:',
            'ratios':'Double values are:','texts':'String values are:'}
    result=[('cli:param/list','param_list',['param','list',node],sorted(data))]
    result += [('cli:param/get','param_get_'+name,['param','get',node,name],{'label':label,'value':data[name]}) for name,label in labels.items()]
    result += [
        ('cli:param/describe','param_describe_count',['param','describe',node,'count'],['Parameter name: count','Type: integer','Description: Bounded counter','Constraints:','Min value: 0','Max value: 100','Step: 1','Additional constraints: whole steps']),
        ('cli:param/describe','param_describe_locked',['param','describe',node,'locked'],['Parameter name: locked','Type: integer','Description: Immutable marker','Constraints:','Read only: true']),
        ('cli:param/dump','param_dump',['param','dump',node],{'node':node,'values':data}),
    ]
    return result


def typed_equal(actual,expected):
    if type(actual) is not type(expected):return False
    if isinstance(expected,dict):return set(actual)==set(expected) and all(typed_equal(actual[k],v) for k,v in expected.items())
    if isinstance(expected,list):return len(actual)==len(expected) and all(typed_equal(a,b) for a,b in zip(actual,expected))
    return actual==expected


def oracle(case,stdout,expected):
    lines=[line.strip() for line in stdout.splitlines() if line.strip()]
    if case=='cli:param/list':return sorted(lines)==sorted(expected)
    if case=='cli:param/describe':return lines==expected
    if case=='cli:param/get':
        label=expected['label']
        if len(lines)!=1 or not lines[0].startswith(label+' '):return False
        payload=lines[0][len(label)+1:]
        if label=='String value is:':value=payload
        else:
            try:value=ast.literal_eval(payload)
            except (ValueError,SyntaxError):return False
        if label=='Byte values are:':
            if not isinstance(value,list) or any(type(v) is not bytes or len(v)!=1 for v in value):return False
            value=[v[0] for v in value]
        return typed_equal(value,expected['value'])
    if case=='cli:param/dump':
        try:data=yaml.safe_load(stdout)
        except yaml.YAMLError:return False
        if not isinstance(data,dict) or set(data)!={expected['node']}:return False
        node=data[expected['node']]
        if not isinstance(node,dict) or set(node)!={'ros__parameters'}:return False
        params=node['ros__parameters']
        if not isinstance(params,dict):return False
        octets=params.get('octets')
        if not isinstance(octets,list) or any(type(v) is not bytes or len(v)!=1 for v in octets):return False
        params['octets']=[v[0] for v in octets]
        return typed_equal(params,expected['values'])
    return False
