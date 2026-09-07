"""Compare transformed raw storage with the original recorded samples."""
import json
import yaml
import cli_acceptance as a
from bag_contract import read_sqlite,read_mcap
from bag_transform import check_records
from verify_bags import local_file


def read(root,board,directory,storage):
    metadata=yaml.safe_load(local_file(root,board,'bags/'+directory+'/metadata.yaml').read_text())['rosbag2_bagfile_information']
    names=metadata['relative_file_paths']
    if len(names)!=1 or '/' in names[0] or '\\' in names[0]:raise ValueError('invalid transformed data path')
    relative='bags/'+directory+'/'+names[0];file=local_file(root,board,relative)
    types,rows=(read_sqlite if storage=='sqlite3' else read_mcap)(file)
    return relative,types,rows,metadata


def validate(value,root,run,board):
    for record in value['results']:
        if record['case_id'] not in ('cli:bag/convert','cli:bag/reindex'):continue
        e=record['expected'];relative,types,rows,metadata=read(root,board,e['destination'],e['target'])
        source_file,source_types,source_rows,_=read(root,board,e['source'],e['source'])
        check_records(source_types,source_rows,types,rows,e['selected'])
        stamps=[r[1] for r in rows]
        if metadata['storage_identifier']!=e['target'] or metadata['message_count']!=len(rows) or metadata['starting_time']['nanoseconds_since_epoch']!=min(stamps) or metadata['duration']['nanoseconds']!=max(stamps)-min(stamps):raise ValueError('transformed metadata differs')
        expected_topics=sorted((*t,sum(r[0]==t[0] for r in rows)) for t in types)
        actual_topics=sorted((t['topic_metadata']['name'],t['topic_metadata']['type'],t['topic_metadata']['serialization_format'],t['message_count']) for t in metadata['topics_with_message_count'])
        if actual_topics!=expected_topics:raise ValueError('transformed metadata topics differ')
        original=a.digest(local_file(root,board,source_file).read_bytes())
        preparation={'source_file':source_file,'source_sha256':original,'destination':e['destination'],'metadata_absent':True}
        if e['operation']=='reindex':
            preparation.update(copy_file=relative,copy_sha256=original)
            if a.digest(local_file(root,board,relative).read_bytes())!=original:raise ValueError('reindex changed data file')
        else:
            options_file=root/(board+'.'+e['label']+'.yaml');preparation['options_sha256']=a.digest(options_file.read_bytes())
            remote='/data/local/tmp/ros2/.mdds-owned-runs/'+run
            options={'uri':remote+'/bags/'+e['destination'],'storage_id':e['target'],'topics':[e['selected']]}
            if e['target']=='mcap':options['storage_config_uri']=remote+'/mcap_config.yaml'
            if yaml.safe_load(options_file.read_text())!={'output_bags':[options]}:raise ValueError('conversion options differ')
        if record['transform_preparation']!=preparation:raise ValueError('transformation input or absence proof differs')
        inspection=json.loads((root/(board+'.'+e['label']+'.inspection.json')).read_text())
        if inspection!=record['transform_inspection'] or inspection['metadata']!=metadata or sorted(tuple(t) for t in inspection['types'])!=sorted(types) or sorted(tuple(r) for r in inspection['records'])!=sorted(rows):raise ValueError('transformed native inspection differs')
