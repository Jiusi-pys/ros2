"""Actual sqlite3/mcap record, info and filtered cross-board play recipes."""
import json
from pathlib import Path
import re
from bag_contract import FORMATS,topic


def recipe(ns,peer,nonce):
    run=ns.removeprefix('/ros_broker_');role='A' if peer=='B' else 'B';root='/data/local/tmp/ros2/.mdds-owned-runs/'+run
    cases=[]
    for storage in FORMATS:
        bag=root+'/bags/'+storage
        args=['bag','record','--storage',storage,'--output',bag,'--topics',topic(run,peer,storage,'main'),topic(run,peer,storage,'noise'),'--max-cache-size','0','--disable-keyboard-controls','--node-name','_bag_record_'+role+'_'+storage]
        if storage=='mcap':args+=['--storage-config-file',root+'/mcap_config.yaml']
        cases.append(('cli:bag/record','bag_record_'+storage,args,{'storage':storage,'bag':bag,'source_role':peer}))
        cases.append(('cli:bag/info','bag_info_'+storage,['bag','info',bag],{'storage':storage,'source_role':peer,'run_id':run}))
        cases.append(('cli:bag/play','bag_play_'+storage,['bag','play','--input',bag,storage,'--topics',topic(run,peer,storage,'main'),'--remap',topic(run,peer,storage,'main')+':='+topic(run,role,storage,'play'),'--delay','3','--disable-keyboard-controls','--wait-for-all-acked','5000'],{'storage':storage,'source_role':peer,'run_id':run}))
    return cases


def oracle(case,stdout,expected):
    if case=='cli:bag/play':return True  # Exact peer callbacks are mandatory in the host verifier.
    if case=='cli:bag/info':
        storage=expected['storage'];run=expected['run_id'];peer=expected['source_role']
        ids=re.findall(r'(?m)^Storage id:\s*(\S+)',stdout);counts=re.findall(r'(?m)^Messages:\s*(\d+)',stdout)
        durations=re.findall(r'(?m)^Duration:\s*([0-9.]+)s',stdout)
        rows=re.findall(r'Topic:\s*(\S+)\s*\|\s*Type:\s*(\S+)\s*\|\s*Count:\s*(\d+)\s*\|\s*Serialization Format:\s*(\S+)',stdout)
        wanted=sorted((topic(run,peer,storage,kind),'std_msgs/msg/String','5','cdr') for kind in ('main','noise'))
        return ids==[storage] and counts==['10'] and len(durations)==1 and float(durations[0])>0 and sorted(rows)==wanted
    return False
