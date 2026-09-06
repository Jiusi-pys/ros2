"""Actual component-container CLI contract."""
import re

TYPES=['composition::Talker','composition::Listener','composition::NodeLikeListener','composition::Server','composition::Client']


def namespace(ns):return '/components_'+ns.removeprefix('/ros_broker_')


def containers(ns):return [namespace(ns)+'/container_'+role for role in ('A','B')]


def recipe(ns,peer):
    space=namespace(ns);container=space+'/container_'+peer
    nodes={1:space+'/primary_'+peer,2:space+'/survivor_'+peer}
    result=[('cli:component/types','component_types',['component','types','composition'],{'kind':'types'})]
    for id_,name in [(1,'primary'),(2,'survivor')]:
        result.append(('cli:component/load','component_load_'+name,['component','load',container,'composition','composition::Talker','-n',name+'_'+peer,'--node-namespace',space,'-r','chatter:='+space+'/'+peer+'/'+name+'/out','--quiet'],{'kind':'rows','rows':[[id_,nodes[id_]]]}))
    result.extend([
        ('cli:component/list','component_list_loaded',['component','list',container],{'kind':'rows','rows':[[1,nodes[1]],[2,nodes[2]]]}),
        ('cli:component/list','component_containers',['component','list','--containers-only'],{'kind':'names','names':containers(ns)}),
        ('cli:component/unload','component_unload_primary',['component','unload',container,'1','--quiet'],{'kind':'id','id':1}),
        ('cli:component/unload','component_list_survivor',['component','list',container],{'kind':'rows','rows':[[2,nodes[2]]]}),
        ('cli:component/unload','component_unload_survivor',['component','unload',container,'2','--quiet'],{'kind':'id','id':2}),
        ('cli:component/unload','component_list_empty',['component','list',container],{'kind':'rows','rows':[]}),
    ])
    return result


def oracle(stdout,expected):
    lines=[line.strip() for line in stdout.splitlines() if line.strip()]
    if expected['kind']=='types':return sorted(lines)==sorted(TYPES)
    if expected['kind']=='names':return sorted(lines)==sorted(expected['names'])
    if expected['kind']=='id':return lines==[str(expected['id'])]
    if expected['kind']=='rows':
        rows=[]
        for line in lines:
            match=re.fullmatch(r'(\d+)\s+(/\S+)',line)
            if not match:return False
            rows.append([int(match[1]),match[2]])
        return sorted(rows)==sorted(expected['rows'])
    return False
