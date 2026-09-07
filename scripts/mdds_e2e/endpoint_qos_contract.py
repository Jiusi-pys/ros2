"""Explicit RxO matrix: discovered endpoints may still be incompatible."""
import json

INF=(1<<63)-1
MATRIX=[
    {'name':'reliable','offered':{},'requested':{},'compatible':True},
    {'name':'reliability_bad','offered':{'reliability':2},'requested':{},'compatible':False},
    {'name':'reliability_relaxed','offered':{},'requested':{'reliability':2},'compatible':True},
    {'name':'durability_bad','offered':{},'requested':{'durability':1},'compatible':False},
    {'name':'durability_compatible','offered':{'durability':1},'requested':{},'compatible':True},
    {'name':'deadline_bad','offered':{'deadline':2_000_000_000},'requested':{'deadline':1_000_000_000},'compatible':False},
    {'name':'deadline_compatible','offered':{'deadline':1_000_000_000},'requested':{'deadline':2_000_000_000},'compatible':True}]

def row(name):return next(v for v in MATRIX if v['name']==name)
def topic(run,role,name):return '/endpoint_qos_'+run+'/'+role+'/'+name
def payloads(run,nonce,role,name):return ['|'.join((run,nonce,role,name,str(i))) for i in range(3)]

def qos(name,direction):
    index=next(i for i,v in enumerate(MATRIX) if v['name']==name)
    return {'history':1,'depth':(5 if direction=='offered' else 13)+index,'reliability':1,'durability':2,
            'deadline':INF,'lifespan':INF,'liveliness':1,'lease':INF,**row(name)[direction]}

def validate_counts(name,value):
    matched=int(row(name)['compatible'])
    if value!={'publishers':1,'subscriptions':1,'writer_matches':matched,'reader_matches':matched}:raise ValueError('graph visibility or QoS match count differs: '+name)

def validate_received(name,received,run,nonce,peer):
    if received!=(payloads(run,nonce,peer,name) if row(name)['compatible'] else []):raise ValueError('QoS-filtered peer payloads differ: '+name)

def validate(value,run,nonce,role,type_hash):
    peer='B' if role=='A' else 'A';names={v['name'] for v in MATRIX};gids=[]
    if any(value.get(k)!=v for k,v in {'run_id':run,'nonce':nonce,'role':role,'passed':True}.items()):raise ValueError('endpoint QoS identity differs')
    if any(set(value[key])!=names for key in ('counts','metadata','sent','received','compatibility')):raise ValueError('QoS matrix cases missing')
    if type(value.get('observation_ns')) is not int or value['observation_ns']<1_000_000_000:raise ValueError('QoS negative observation window missing')
    for name in names:
        compatibility=value['compatibility'][name]
        if compatibility.get('code')!=(0 if row(name)['compatible'] else 2):raise ValueError('QoS compatibility query disagrees with matching')
        if row(name)['compatible']:
            if compatibility.get('reason')!='':raise ValueError('compatible QoS query reported a problem')
        elif name.split('_')[0] not in compatibility.get('reason','').lower():raise ValueError('QoS incompatibility reason differs')
        validate_counts(name,value['counts'][name]);validate_received(name,value['received'][name],run,nonce,peer)
        if value['sent'][name]!=payloads(run,nonce,role,name):raise ValueError('QoS send control differs')
        for kind,direction,owner,number in (('publisher','offered',peer,1),('subscription','requested',role,2)):
            item=value['metadata'][name][kind];gid=item['gid']
            wanted={'node':'alpha_'+owner,'namespace':'/ros_broker_'+run,'type':'std_msgs/msg/String','type_hash':type_hash,'direction':number,'qos':qos(name,direction),'gid':gid}
            if json.dumps(item,sort_keys=True)!=json.dumps(wanted,sort_keys=True):raise ValueError('endpoint metadata differs: '+name+'/'+kind)
            if len(gid)!=16 or not any(gid) or any(type(v) is not int or not 0<=v<=255 for v in gid):raise ValueError('endpoint GID invalid')
            gids.append(tuple(gid))
    if len(gids)!=len(set(gids)):raise ValueError('QoS endpoints reuse GIDs')
