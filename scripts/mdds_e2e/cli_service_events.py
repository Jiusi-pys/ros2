"""Check all four service-introspection events against the real client identity."""
import yaml

KINDS=('REQUEST_SENT','REQUEST_RECEIVED','RESPONSE_SENT','RESPONSE_RECEIVED')


def events_match(stdout,expected):
    gid=expected.get('client_gid',[]);sequence=expected.get('sequence_number')
    if len(gid)!=16 or not any(gid) or any(type(n) is not int or not 0<=n<=255 for n in gid) or type(sequence) is not int or sequence<=0:return False
    warning="No publishers on topic '"+expected['service']+"/_service_event'; is service introspection on the client or server enabled?"
    stdout='\n'.join(line for line in stdout.splitlines() if line!=warning)
    try:events=[event for event in yaml.safe_load_all(stdout) if event is not None]
    except yaml.YAMLError:return False
    if len(events)!=4:return False
    seen=set()
    for event in events:
        if not isinstance(event,dict) or set(event)!={'info','request','response'}:return False
        info=event['info']
        if not isinstance(info,dict) or set(info)!={'event_type','stamp','client_gid','sequence_number'}:return False
        kind=info['event_type']
        if kind not in KINDS or kind in seen:return False
        seen.add(kind)
        if info['client_gid']!=gid or info['sequence_number']!=sequence:return False
        stamp=info['stamp']
        if not isinstance(stamp,dict) or set(stamp)!={'sec','nanosec'} or type(stamp['sec']) is not int or stamp['sec']<0 or type(stamp['nanosec']) is not int or not 0<=stamp['nanosec']<1000000000:return False
        request=[{'a':expected['a'],'b':expected['b']}] if kind.startswith('REQUEST') else []
        response=[{'sum':expected['sum']}] if kind.startswith('RESPONSE') else []
        if event['request']!=request or event['response']!=response:return False
    return seen==set(KINDS)
