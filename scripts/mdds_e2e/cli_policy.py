"""Exact generated policy permissions for the isolated live ROS fixture."""
import xml.etree.ElementTree as ET


def recipe(ns,peer,nonce):
    root='/data/local/tmp/ros2/.mdds-owned-runs/'+ns.removeprefix('/ros_broker_')
    return [('cli:security/generate_policy','policy_'+mode,['security','generate_policy',root+'/policy_'+mode+'.xml']+([] if mode=='cached' else ['--no-daemon','--spin-time','3']),{'mode':mode,'path':root+'/policy_'+mode+'.xml'}) for mode in ('cached','direct')]


def expected_policy(run):
    ns='/ros_broker_'+run;result={}
    for role in ('A','B'):
        peer='B' if role=='A' else 'A'
        for name in ('alpha','beta'):
            other_name='beta' if name=='alpha' else 'alpha';node=name+'_'+role
            rules={('topic','publish'):{'/parameter_events',f'{ns}/{role}/{name}/out'},
                   ('topic','subscribe'):{f'{ns}/{peer}/{name}/out'},
                   ('service','reply'):{f'{ns}/{node}/get_type_description',f'{ns}/{role}/{name}/serve'},
                   ('service','request'):{f'{ns}/{peer}/{name}/serve'}}
            if name=='alpha':
                rules[('topic','publish')].update({f'{ns}/{role}/cli_source',f'{ns}/{role}/_hidden'})
                rules[('topic','subscribe')].update({f'{ns}/{role}/cli_sink',f'{ns}/{peer}/_hidden'})
                rules[('service','reply')].add(f'{ns}/{role}/_hidden_service')
                rules[('service','request')].add(f'{ns}/{peer}/_hidden_service')
            for suffix in ('feedback','status'):
                rules[('topic','publish')].add(f'{ns}/{role}/{name}/action/_action/{suffix}')
                rules[('topic','subscribe')].add(f'{ns}/{peer}/{other_name}/action/_action/{suffix}')
            for suffix in ('send_goal','get_result','cancel_goal'):
                rules[('service','reply')].add(f'{ns}/{role}/{name}/action/_action/{suffix}')
                rules[('service','request')].add(f'{ns}/{peer}/{other_name}/action/_action/{suffix}')
            result[('/'+role+'/'+name,ns,node)]=rules
        result[('/'+role+'/alpha',ns,'duplicate_'+role)]={('topic','publish'):{'/parameter_events'},('service','reply'):{ns+'/duplicate_'+role+'/get_type_description'}}
    return result


def parse_policy(raw):
    if len(raw)>2*1024*1024 or '<!DOCTYPE' in raw:raise ValueError('unsupported policy input')
    try:root=ET.fromstring(raw)
    except ET.ParseError as error:raise ValueError('malformed policy') from error
    if root.tag!='policy' or root.attrib!={'version':'0.2.0'} or [c.tag for c in root]!=['enclaves']:raise ValueError('policy root differs')
    result={}
    for enclave in root[0]:
        if enclave.tag!='enclave' or set(enclave.attrib)!={'path'} or [c.tag for c in enclave]!=['profiles']:raise ValueError('invalid enclave')
        for profile in enclave[0]:
            if profile.tag!='profile' or set(profile.attrib)!={'ns','node'}:raise ValueError('invalid profile')
            ns,node=profile.get('ns'),profile.get('node');key=(enclave.get('path'),ns,node)
            if key in result:raise ValueError('duplicate profile')
            rules={}
            for group in profile:
                if group.tag not in ('topics','services') or len(group.attrib)!=1:raise ValueError('unsupported permission group')
                kind=group.tag[:-1];direction,allow=next(iter(group.attrib.items()))
                if allow!='ALLOW' or direction not in (('publish','subscribe') if kind=='topic' else ('request','reply')):raise ValueError('unsupported permission direction')
                rule=(kind,direction)
                if rule in rules:raise ValueError('duplicate permission group')
                names=set()
                for item in group:
                    if item.tag!=kind or item.attrib or len(item) or not item.text:raise ValueError('invalid permission expression')
                    value=item.text
                    if value.startswith('~/'):value=ns.rstrip('/')+'/'+node+value[1:]
                    elif not value.startswith('/'):value=ns.rstrip('/')+'/'+value
                    if value in names:raise ValueError('duplicate permission expression')
                    names.add(value)
                rules[rule]=names
            result[key]=rules
    return result


def validate_policy(raw,expected):
    actual=parse_policy(raw)
    if actual!=expected:
        missing={str(key):{str(rule):sorted(names-actual.get(key,{}).get(rule,set())) for rule,names in rules.items() if names-actual.get(key,{}).get(rule,set())} for key,rules in expected.items()}
        extra={str(key):{str(rule):sorted(names-expected.get(key,{}).get(rule,set())) for rule,names in rules.items() if names-expected.get(key,{}).get(rule,set())} for key,rules in actual.items()}
        raise ValueError('policy permissions differ: missing='+str({k:v for k,v in missing.items() if v})+' extra='+str({k:v for k,v in extra.items() if v}))
    return actual
