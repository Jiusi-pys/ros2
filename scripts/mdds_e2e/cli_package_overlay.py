"""Freeze and verify the pure Python ros2cli package for board acceptance."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile
from doctor_runtime import extract_wheel


def digest(raw):return hashlib.sha256(raw).hexdigest()


def pack(root):
    files=[];archive=root/'ros2cli_overlay.zip';packages=['ros2cli','ros2multicast']
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as zipped:
        for package in packages:
            source=Path.cwd()/'src/ros2/ros2cli'/package/package
            for path in sorted(source.rglob('*.py')):
                if '__pycache__' in path.parts:continue
                if path.is_symlink():raise ValueError('linked CLI source')
                name=package+'/'+path.relative_to(source).as_posix();raw=path.read_bytes()
                files.append({'path':name,'sha256':digest(raw),'size':len(raw)});zipped.writestr(name,raw)
    manifest={'schema_version':1,'packages':packages,'archive_sha256':digest(archive.read_bytes()),'files':files}
    (root/'ros2cli_overlay.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')


def prepare(root,expected):
    if root.parent!=Path('/data/local/tmp/ros2/.mdds-owned-runs') or (root/'owner').read_text()!=f'MDDS_RUN_OWNER RUN_ID={root.name} LABEL=ros_broker\n':raise ValueError('wrong CLI overlay owner')
    raw=(root/'ros2cli_overlay.json').read_bytes()
    if digest(raw)!=expected:raise ValueError('CLI overlay manifest differs')
    manifest=json.loads(raw)
    if manifest['schema_version']!=1 or not 1<=len(manifest['files'])<=128:raise ValueError('CLI overlay inventory invalid')
    site=root/'python';extract_wheel(root/'ros2cli_overlay.zip',site,manifest['archive_sha256'])
    for item in manifest['files']:
        path=site/item['path']
        if not any(path.resolve().is_relative_to((site/package).resolve()) for package in ('ros2cli','ros2multicast')) or path.stat().st_size!=item['size'] or digest(path.read_bytes())!=item['sha256']:raise ValueError('CLI overlay source differs')
    result={'run_id':root.name,'manifest_sha256':expected,'files':len(manifest['files'])}
    (root/'ros2cli_overlay_ready.json').write_text(json.dumps(result)+'\n')
    print('ROS2CLI_OVERLAY_READY '+expected,flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('operation',choices=('pack','prepare'));parser.add_argument('root',type=Path);parser.add_argument('--sha');args=parser.parse_args()
    if args.operation=='pack':pack(args.root)
    else:prepare(args.root,args.sha)
