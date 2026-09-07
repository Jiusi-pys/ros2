"""Freeze diagnostic dependencies and an unmodified upstream rosdistro snapshot."""
import argparse
import hashlib
import importlib
import importlib.metadata
import json
from pathlib import Path,PurePosixPath
import re
import shutil
import sys
import urllib.request
import zipfile

PROJECTS={'rosdistro','rospkg','distro'}


def digest(raw):return hashlib.sha256(raw).hexdigest()


def extract_wheel(wheel,site,sha):
    raw=wheel.read_bytes()
    if len(raw)>4*1024*1024 or digest(raw)!=sha:raise ValueError('doctor wheel digest/size differs')
    with zipfile.ZipFile(wheel) as archive:
        entries=archive.infolist()
        if len(entries)>1024 or sum(i.file_size for i in entries)>32*1024*1024:raise ValueError('doctor wheel exceeds extraction bounds')
        for info in entries:
            name=PurePosixPath(info.filename)
            if name.is_absolute() or '..' in name.parts or ':' in info.filename or (info.external_attr>>16)&0o170000==0o120000:raise ValueError('unsafe doctor wheel member')
            path=site.joinpath(*name.parts)
            if not path.resolve().is_relative_to(site.resolve()):raise ValueError('doctor wheel escapes stage')
            if info.is_dir():path.mkdir(parents=True,exist_ok=True);continue
            path.parent.mkdir(parents=True,exist_ok=True)
            with path.open('xb') as out:out.write(archive.read(info))


def pack(output,hello=False):
    workspace=Path.cwd();lock=workspace/'scripts/python/ohos_python.lock.json'
    wheels=[p for p in json.loads(lock.read_text(encoding='utf-8'))['wheels'] if p['project'] in PROJECTS]
    if {p['project'] for p in wheels}!=PROJECTS:raise ValueError('doctor dependencies are not locked')
    for item in wheels:
        source=workspace/'python_target/wheels'/item['filename']
        if digest(source.read_bytes())!=item['sha256']:raise ValueError('doctor source wheel differs')
        shutil.copyfile(source,output/item['filename'])
    reference=json.loads((workspace/'scripts/python/rosdistro_reference.lock.json').read_text(encoding='utf-8'))
    if {f['path'] for f in reference['files']}!={'index-v4.yaml','jazzy/distribution.yaml'}:raise ValueError('wrong rosdistro reference files')
    for item in reference['files']:
        cache=workspace/'python_target/rosdistro_reference'/item['path'];cache.parent.mkdir(parents=True,exist_ok=True)
        if not cache.exists():
            with urllib.request.urlopen(item['url'],timeout=20) as response:raw=response.read(4*1024*1024)
            if digest(raw)!=item['sha256'] or len(raw)!=item['size']:raise ValueError('downloaded rosdistro reference differs')
            cache.write_bytes(raw)
        raw=cache.read_bytes()
        if digest(raw)!=item['sha256'] or len(raw)!=item['size']:raise ValueError('cached rosdistro reference differs')
        target=output/'doctor_reference'/item['path'];target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(raw)
    template=(workspace/'scripts/env_ohos.template.sh').read_text(encoding='utf-8')
    environment={key:re.findall(r'(?m)^export '+key+r'=(\w+)$',template) for key in ('ROS_DISTRO','ROS_VERSION','ROS_PYTHON_VERSION')}
    if environment!={'ROS_DISTRO':['jazzy'],'ROS_VERSION':['2'],'ROS_PYTHON_VERSION':['3']}:raise ValueError('deployment distribution identity differs')
    env=''.join('export '+key+'='+value[0]+'\n' for key,value in environment.items())
    (output/'doctor_environment.env').write_text(env,encoding='utf-8',newline='\n')
    manifest={'schema_version':1,'python_lock_sha256':digest(lock.read_bytes()),'wheels':wheels,'reference':reference,'environment_sha256':digest(env.encode())}
    if hello:
        package=workspace/'src/ros2/ros2cli/ros2doctor/ros2doctor';files=[];archive=output/'doctor_application.zip'
        with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as zipped:
            for path in sorted(package.rglob('*.py')):
                if '__pycache__' in path.parts:continue
                if path.is_symlink():raise ValueError('linked doctor source')
                name='ros2doctor/'+path.relative_to(package).as_posix();raw=path.read_bytes()
                files.append({'path':name,'sha256':digest(raw),'size':len(raw)});zipped.writestr(name,raw)
        manifest['application']={'archive':'doctor_application.zip','sha256':digest(archive.read_bytes()),'files':files}
    (output/'doctor_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')


def prepare(root,sha):
    if root.parent!=Path('/data/local/tmp/ros2/.mdds-owned-runs') or (root/'owner').read_text()!=f'MDDS_RUN_OWNER RUN_ID={root.name} LABEL=ros_broker\n':raise ValueError('wrong doctor runtime owner')
    raw=(root/'doctor_manifest.json').read_bytes()
    if digest(raw)!=sha:raise ValueError('doctor manifest digest differs')
    manifest=json.loads(raw)
    if manifest['schema_version']!=1 or {p['project'] for p in manifest['wheels']}!=PROJECTS:raise ValueError('doctor manifest inventory differs')
    site=root/'python'
    for item in manifest['wheels']:
        if Path(item['filename']).name!=item['filename']:raise ValueError('unsafe doctor wheel filename')
        extract_wheel(root/item['filename'],site,item['sha256'])
    application=manifest.get('application')
    if application:
        if application['archive']!='doctor_application.zip':raise ValueError('wrong doctor application archive')
        extract_wheel(root/application['archive'],site,application['sha256'])
        for item in application['files']:
            path=site/item['path']
            if not path.resolve().is_relative_to((site/'ros2doctor').resolve()) or digest(path.read_bytes())!=item['sha256']:raise ValueError('doctor application source differs')
    for item in manifest['reference']['files']:
        if item['path'] not in ('index-v4.yaml','jazzy/distribution.yaml'):raise ValueError('unsafe reference filename')
        data=(root/'doctor_reference'/item['path']).read_bytes()
        if digest(data)!=item['sha256'] or len(data)!=item['size']:raise ValueError('staged reference differs')
    if digest((root/'doctor_environment.env').read_bytes())!=manifest['environment_sha256']:raise ValueError('doctor environment differs')
    sys.path.insert(0,str(site));modules={}
    for item in manifest['wheels']:
        module=importlib.import_module(item['project']);version=importlib.metadata.version(item['project'])
        if not Path(module.__file__).resolve().is_relative_to(site.resolve()) or version!=item['version']:raise ValueError('doctor module provenance differs')
        modules[item['project']]={'version':version,'file':module.__file__}
    value={'run_id':root.name,'manifest_sha256':sha,'modules':modules}
    if application:value['application_sha256']=application['sha256']
    (root/'doctor_runtime.json').write_text(json.dumps(value)+'\n')
    print('DOCTOR_RUNTIME_READY '+sha,flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('operation',choices=('pack','prepare'));parser.add_argument('root',type=Path);parser.add_argument('--sha');parser.add_argument('--hello',action='store_true');args=parser.parse_args()
    if args.operation=='pack':pack(args.root,args.hello)
    else:prepare(args.root,args.sha)
