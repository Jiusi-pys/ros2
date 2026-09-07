"""Verify complete diagnostics and live graph facts against frozen references."""
import json
import re
import cli_acceptance as a


def validate(value,root,run,board):
    manifest=json.loads((root/'doctor_manifest.json').read_text());sha=a.digest((root/'doctor_manifest.json').read_bytes())
    runtime=json.loads((root/(board+'.doctor_runtime.json')).read_text())
    remote='/data/local/tmp/ros2/.mdds-owned-runs/'+run
    modules={w['project']:{'version':w['version'],'file':remote+'/python/'+w['project']+'/__init__.py'} for w in manifest['wheels']}
    if runtime!={'run_id':run,'manifest_sha256':sha,'modules':modules}:raise ValueError('doctor dependency provenance differs')
    for item in manifest['reference']['files']:
        data=(root/'doctor_reference'/item['path']).read_bytes()
        if a.digest(data)!=item['sha256'] or len(data)!=item['size']:raise ValueError('rosdistro snapshot differs')
    if a.digest((root/'doctor_environment.env').read_bytes())!=manifest['environment_sha256']:raise ValueError('doctor distribution identity differs')
    expected={f'/ros_broker_{run}/{role}/{name}/out':(1,1) for role in ('A','B') for name in ('alpha','beta')}
    observed=[]
    for entry in value['results']:
        if entry['case_id'] not in ('cli:doctor','cli:wtf'):continue
        e=entry['execution'];raw=a.read_artifact({**e['log'],'path':board+'.'+e['log']['path']},root).decode()
        if any(s in raw for s in ('fails to load','Fail to call','Unable to instantiate','ROS_DISTRO is not set','Missing rosdistro')):raise ValueError('doctor omitted or failed an entry point')
        if entry['expected']['kind']=='report':
            rows=re.findall(r'(?m)^topic\s*:\s*(\S+)\s*\n\s*publisher count\s*:\s*(\d+)\s*\n\s*subscriber count\s*:\s*(\d+)',raw)
            topics={name:(int(pubs),int(subs)) for name,pubs,subs in rows}
            if any(topics.get(name)!=counts for name,counts in expected.items()):raise ValueError('doctor live topic counts differ')
            pairs=re.findall(r'(?m)^topic \[type\]\s*:\s*(\S+) \[([^]]+)\]\s*\n\s*publisher node\s*:\s*(\S+)\s*\n\s*subscriber node\s*:\s*(\S+)\s*\n\s*compatibility status\s*:\s*(\S+)',raw)
            for role in ('A','B'):
                peer='B' if role=='A' else 'A'
                for name in ('alpha','beta'):
                    wanted=(f'/ros_broker_{run}/{role}/{name}/out','std_msgs/msg/String',name+'_'+role,name+'_'+peer,'OK')
                    if pairs.count(wanted)!=1:raise ValueError('doctor QoS pair report differs')
            observed.append({name:topics[name] for name in expected})
    if len(observed)!=2 or observed[0]!=observed[1]:raise ValueError('doctor alias reports differ')
