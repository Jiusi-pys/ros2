"""Real CLI conversion and metadata reconstruction of run-owned bag copies."""
import hashlib
import json
from pathlib import Path
import shutil
import yaml
from bag_contract import FORMATS,topic
from cli_bag import recipe as recording_recipe


def check_records(source_types,source_rows,actual_types,actual_rows,selected):
    wanted_types=[t for t in source_types if selected is None or t[0]==selected]
    wanted_rows=[r for r in source_rows if selected is None or r[0]==selected]
    if not wanted_rows or sorted(wanted_types)!=sorted(actual_types) or sorted(wanted_rows)!=sorted(actual_rows):
        raise ValueError('transformed types, samples or timestamps differ')


def recipe(ns,peer,nonce):
    cases=recording_recipe(ns,peer,nonce);run=ns.removeprefix('/ros_broker_');root='/data/local/tmp/ros2/.mdds-owned-runs/'+run
    for source in FORMATS:
        target='mcap' if source=='sqlite3' else 'sqlite3'
        label='bag_convert_'+source+'_to_'+target
        expected={'operation':'convert','source':source,'target':target,'destination':'converted_'+target,'selected':topic(run,peer,source,'main'),'label':label}
        cases.append(('cli:bag/convert',label,['bag','convert','--input',root+'/bags/'+source,source,'--output-options',root+'/'+label+'.yaml'],expected))
        label='bag_reindex_'+source
        expected={'operation':'reindex','source':source,'target':source,'destination':'reindex_'+source,'selected':None,'label':label}
        cases.append(('cli:bag/reindex',label,['bag','reindex',root+'/bags/reindex_'+source,'--storage',source],expected))
    return cases


def prepare(root,expected):
    source=root/'bags'/expected['source'];destination=root/'bags'/expected['destination']
    metadata=yaml.safe_load((source/'metadata.yaml').read_text())['rosbag2_bagfile_information']
    names=metadata['relative_file_paths']
    if len(names)!=1 or Path(names[0]).name!=names[0]:raise ValueError('invalid source data path')
    original=source/names[0]
    if original.is_symlink() or not original.is_file() or destination.exists():raise ValueError('invalid bag transform inputs')
    proof={'source_file':original.relative_to(root).as_posix(),'source_sha256':hashlib.sha256(original.read_bytes()).hexdigest(),'destination':expected['destination'],'metadata_absent':not (destination/'metadata.yaml').exists()}
    if expected['operation']=='convert':
        options={'uri':str(destination),'storage_id':expected['target'],'topics':[expected['selected']]}
        if expected['target']=='mcap':options['storage_config_uri']=str(root/'mcap_config.yaml')
        with (root/(expected['label']+'.yaml')).open('x') as out:yaml.safe_dump({'output_bags':[options]},out)
        proof['options_sha256']=hashlib.sha256((root/(expected['label']+'.yaml')).read_bytes()).hexdigest()
    else:
        destination.mkdir();copy=destination/original.name;shutil.copyfile(original,copy)
        proof['copy_file']=copy.relative_to(root).as_posix();proof['copy_sha256']=hashlib.sha256(copy.read_bytes()).hexdigest()
    return proof


def inspect(root,expected):
    import rosbag2_py
    from rclpy.serialization import deserialize_message
    from std_msgs.msg import String
    destination=root/'bags'/expected['destination'];reader=rosbag2_py.SequentialReader()
    reader.open(rosbag2_py.StorageOptions(uri=str(destination),storage_id=expected['target']),rosbag2_py.ConverterOptions('',''))
    types=sorted((m.name,m.type,m.serialization_format) for m in reader.get_all_topics_and_types());records=[]
    while reader.has_next():
        name,data,stamp=reader.read_next();records.append((name,stamp,deserialize_message(data,String).data))
        if len(records)>10:raise ValueError('unexpected transformed record count')
    del reader
    metadata=yaml.safe_load((destination/'metadata.yaml').read_text())['rosbag2_bagfile_information']
    value={'types':types,'records':records,'metadata':metadata}
    (root/(expected['label']+'.inspection.json')).write_text(json.dumps(value)+'\n')
    return value
