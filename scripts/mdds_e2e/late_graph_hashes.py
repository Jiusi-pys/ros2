"""Bind every inspected endpoint type to generated ROS type descriptions."""
import hashlib
import json
from pathlib import Path
import sys
from late_graph_contract import NODES,endpoint_specs


def pack(root):
    names={v['type'] for name in NODES for v in endpoint_specs('run','A',name).values()}
    value={'hashes':{},'sources':{}}
    for name in sorted(names):
        package,kind,type_name=name.split('/')
        path=Path('build_ohos')/package/'rosidl_generator_type_description'/package/kind/(type_name+'.json')
        raw=path.read_bytes();hashes=[v['hash_string'] for v in json.loads(raw)['type_hashes'] if v['type_name']==name]
        if len(hashes)!=1:raise ValueError('type hash missing: '+name)
        value['hashes'][name]=hashes[0];value['sources'][name]={'path':path.as_posix(),'sha256':hashlib.sha256(raw).hexdigest()}
    (root/'late_graph_hashes.json').write_text(json.dumps(value,indent=2)+'\n')


if __name__=='__main__':pack(Path(sys.argv[1]))
