"""Parameter mutation recipes; verification reads stay inside each feature receipt."""
from cli_parameters import values,oracle as read_oracle


def loaded_values(nonce,role):
    return {'count':values(nonce,role)['count']+2,'flags':[False,True],'ratio':2.75,'texts':['loaded_'+nonce[:8],role]}


def recipe(ns,peer,nonce):
    node=ns+'/alpha_'+peer;seed=values(nonce,peer);loaded=loaded_values(nonce,peer)
    path='/data/local/tmp/ros2/.mdds-owned-runs/'+ns.removeprefix('/ros_broker_')+'/cli_daemon/parameter_load.yaml'
    message=lambda text:{'kind':'message','text':text}
    get=lambda label,value:{'kind':'get','label':label,'value':value}
    result=[
        ('cli:param/set','param_set',['param','set',node,'count',str(seed['count']+1)],message('Set parameter successful')),
        ('cli:param/set','param_set_readback',['param','get',node,'count'],get('Integer value is:',seed['count']+1)),
        ('cli:param/set','param_restore',['param','set',node,'count',str(seed['count'])],message('Set parameter successful')),
        ('cli:param/set','param_restore_readback',['param','get',node,'count'],get('Integer value is:',seed['count'])),
        ('cli:param/load','param_load',['param','load',node,path],{'kind':'lines','lines':['Set parameter '+key+' successful' for key in sorted(loaded)]}),
    ]
    for name,label in [('count','Integer value is:'),('flags','Boolean values are:'),('ratio','Double value is:'),('texts','String values are:')]:
        result.append(('cli:param/load','param_loaded_'+name,['param','get',node,name],get(label,loaded[name])))
    result.extend([
        ('cli:param/delete','param_delete',['param','delete',node,'ephemeral'],message('Deleted parameter successfully')),
        ('cli:param/delete','param_deleted_get',['param','get',node,'ephemeral'],{'kind':'absent'}),
        ('cli:param/delete','param_deleted_list',['param','list',node],{'kind':'list','names':sorted(set(seed)-{'ephemeral'})}),
    ])
    return result


def oracle(stdout,expected):
    lines=[line.strip() for line in stdout.splitlines() if line.strip()]
    if expected['kind']=='absent':return not lines
    if expected['kind']=='message':return lines==[expected['text']]
    if expected['kind']=='lines':return lines==expected['lines']
    if expected['kind']=='list':return sorted(lines)==expected['names']
    if expected['kind']=='get':return read_oracle('cli:param/get',stdout,expected)
    return False


def expected_events(ns,peer,nonce):
    node=ns+'/alpha_'+peer;seed=values(nonce,peer);loaded=loaded_values(nonce,peer)
    types={'count':2,'flags':6,'ratio':3,'texts':9}
    changed=[('count',seed['count']+1),('count',seed['count'])]+sorted(loaded.items())
    events=[{'node':node,'changed':[{'name':name,'type':types[name],'value':value}],'deleted':[]} for name,value in changed]
    events.append({'node':node,'changed':[],'deleted':[{'name':'ephemeral','type':0,'value':None}]})
    return events
