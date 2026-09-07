"""Preliminary SDK lifecycle evidence, distinct from complete peer recovery."""
def validate(records,run,nonce,pid):
    if set(records)!={'ready','paused','restored','stop1','stop2'}:raise ValueError('SDK cycle records incomplete')
    for key,value in records.items():
        if any(value.get(k)!=v for k,v in {'run_id':run,'nonce':nonce,'pid':pid}.items()):raise ValueError('SDK cycle ownership differs')
        if value['generation']!=(2 if key in ('restored','stop2') else 1):raise ValueError('SDK driver generation differs')
    for phase,channels in [('ready',1),('paused',0),('restored',1)]:
        if records[phase]['phase']!=phase or records[phase]['channels']!=channels or records[phase]['remote_links']!=channels:raise ValueError('SDK cycle phase/link differs')
    if not records['ready']['now_ms']<records['paused']['now_ms']<records['restored']['now_ms']:raise ValueError('SDK phase ordering differs')
    for key in ('stop1','stop2'):
        if any(records[key][field]!=0 for field in ('channels','pending_retirements','queued_bytes','reassembly_bytes')):raise ValueError('SDK stop left physical resources')
    for field in ('connections','active_ports'):
        if records['paused'][field]<=0 or records['paused'][field]!=records['stop1'][field] or records['paused'][field]!=records['restored'][field]:raise ValueError('SDK pause/restore altered local clients')
