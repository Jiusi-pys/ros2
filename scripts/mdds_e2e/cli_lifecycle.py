"""Lifecycle state/transition contract for a real remote managed node."""
import re

STATES={1:'unconfigured',2:'inactive',3:'active',4:'finalized',10:'configuring',11:'cleaningup',12:'shuttingdown',13:'activating',14:'deactivating'}
STEPS=[('configure',1,10,2),('activate',2,13,3),('deactivate',3,14,2),('cleanup',2,11,1),('shutdown',1,12,4)]
TRANSITIONS={1:[('configure',1,'configuring'),('shutdown',5,'shuttingdown')],
             2:[('cleanup',2,'cleaningup'),('activate',3,'activating'),('shutdown',6,'shuttingdown')],
             3:[('deactivate',4,'deactivating'),('shutdown',7,'shuttingdown')],4:[]}


def recipe(ns,peer):
    node=ns+'/alpha_'+peer
    state=lambda number:{'kind':'state','label':STATES[number],'id':number}
    listing=lambda number:{'kind':'transitions','items':[[label,id_,STATES[number],target] for label,id_,target in TRANSITIONS[number]]}
    result=[('cli:lifecycle/nodes','lifecycle_nodes',['lifecycle','nodes'],{'kind':'nodes','names':[ns+'/alpha_A',ns+'/alpha_B']}),
            ('cli:lifecycle/nodes','lifecycle_count',['lifecycle','nodes','--count-nodes'],{'kind':'count','value':2}),
            ('cli:lifecycle/get','lifecycle_initial',['lifecycle','get',node],state(1)),
            ('cli:lifecycle/list','lifecycle_list_initial',['lifecycle','list',node],listing(1))]
    for label,before,middle,after in STEPS:
        result.append(('cli:lifecycle/set','lifecycle_'+label,['lifecycle','set',node,label],{'kind':'set'}))
        result.append(('cli:lifecycle/set','lifecycle_after_'+label,['lifecycle','get',node],state(after)))
        if label in ('configure','activate','shutdown'):
            result.append(('cli:lifecycle/list','lifecycle_list_'+label,['lifecycle','list',node],listing(after)))
    return result


def expected_callbacks():return [{'callback':label,'previous':{'id':before,'label':STATES[before]}} for label,before,_,_ in STEPS]


def expected_events():
    return [{'start':{'id':start,'label':STATES[start]},'goal':{'id':end,'label':STATES[end]},'transition':{'id':0,'label':''},'timestamp':0}
            for _,before,middle,after in STEPS for start,end in [(before,middle),(middle,after)]]


def oracle(stdout,expected):
    lines=[line.strip() for line in stdout.splitlines() if line.strip()]
    kind=expected['kind']
    if kind=='nodes':return sorted(lines)==sorted(expected['names'])
    if kind=='count':return lines==[str(expected['value'])]
    if kind=='state':return lines==[f"{expected['label']} [{expected['id']}]"]
    if kind=='set':return lines==['Transitioning successful']
    if kind=='transitions':
        if len(lines)%3:return False
        result=[]
        for index in range(0,len(lines),3):
            match=re.fullmatch(r'- (\w+) \[(\d+)\]',lines[index])
            if not match or not lines[index+1].startswith('Start: ') or not lines[index+2].startswith('Goal: '):return False
            result.append([match[1],int(match[2]),lines[index+1][7:],lines[index+2][6:]])
        return sorted(result)==sorted(expected['items'])
    return False
