"""Freeze a complete rosbag2_py package while replacing only the OHOS Python fix."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def digest(data):return hashlib.sha256(data).hexdigest()


def inventory(package):
    result={}
    for path in package.iterdir():
        if path.name=='__pycache__':continue
        if path.is_symlink() or not path.is_file() or not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_.-]*',path.name):raise ValueError('unexpected rosbag package path')
        result[path.name]=path
    return result


def pack(package,init,scope,output):
    files=inventory(package);files['__init__.py']=init;files['_ohos_plugin_scope.py']=scope
    manifest={'schema_version':1,'files':{name:{'sha256':digest(path.read_bytes()),'size':path.stat().st_size} for name,path in sorted(files.items())}}
    output.write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')


def prepare(root,expected_sha):
    if root.parent!=Path('/data/local/tmp/ros2/.mdds-owned-runs') or (root/'owner').read_text()!=f'MDDS_RUN_OWNER RUN_ID={root.name} LABEL=ros_broker\n':raise ValueError('wrong overlay owner')
    raw=(root/'rosbag_manifest.json').read_bytes()
    if digest(raw)!=expected_sha:raise ValueError('wrong rosbag manifest hash')
    manifest=json.loads(raw);files=manifest['files']
    if manifest['schema_version']!=1 or not 2<=len(files)<=64:raise ValueError('invalid rosbag manifest')
    source=inventory(Path('/data/local/tmp/ros2/Lib/site-packages/rosbag2_py'))
    overrides={'__init__.py':root/'rosbag_init.py','_ohos_plugin_scope.py':root/'rosbag_scope.py'}
    if set(source)-set(overrides)!=set(files)-set(overrides):raise ValueError('installed rosbag package file inventory differs')
    target=root/'python/rosbag2_py';target.mkdir(mode=0o700)
    total=0
    for name,item in files.items():
        if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_.-]*',name):raise ValueError('invalid overlay filename')
        path=overrides.get(name,source.get(name))
        if path is None or path.is_symlink():raise ValueError('missing regular overlay input')
        data=path.read_bytes();total+=len(data)
        if len(data)!=item['size'] or len(data)>16*1024*1024 or total>64*1024*1024 or digest(data)!=item['sha256']:raise ValueError('rosbag package input differs: '+name)
        with (target/name).open('xb') as out:out.write(data)
    print('ROSBAG_OVERLAY_READY '+expected_sha,flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('operation',choices=('pack','prepare'));p.add_argument('--package',type=Path);p.add_argument('--init',type=Path);p.add_argument('--scope',type=Path);p.add_argument('--output',type=Path);p.add_argument('--root',type=Path);p.add_argument('--manifest-sha');args=p.parse_args()
    if args.operation=='pack':pack(args.package,args.init,args.scope,args.output)
    else:prepare(args.root,args.manifest_sha)
