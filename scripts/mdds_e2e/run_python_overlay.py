"""Use the production OHOS finalizer for a frozen installed Python demo."""
import hashlib
import json
from pathlib import Path
import shutil
import sys
import zipfile
from doctor_runtime import extract_wheel


def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def pack(root):
    sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
    from finalize_ohos_install import finalize
    prefix=root/'python_demo_install';scripts=prefix/'Lib/demo_nodes_py';scripts.mkdir(parents=True)
    shutil.copyfile('install_ohos/Lib/demo_nodes_py/talker-script.py',scripts/'talker-script.py')
    finalize(prefix)
    shutil.copyfile(scripts/'talker',root/'process_python_talker')
    site=Path('install_ohos/Lib/site-packages');metadata=list(site.glob('demo_nodes_py-*.egg-info'))
    if len(metadata)!=1:raise ValueError('ambiguous installed Python demo metadata')
    files=[]
    with zipfile.ZipFile(root/'run_python.zip','w',zipfile.ZIP_DEFLATED) as archive:
        for directory in (site/'demo_nodes_py',metadata[0]):
            for path in sorted(directory.rglob('*')):
                if not path.is_file() or '__pycache__' in path.parts:continue
                relative=path.relative_to(site).as_posix();data=path.read_bytes()
                archive.writestr(relative,data);files.append({'path':relative,'sha256':sha(path)})
    value={'files':files,'archive_sha256':sha(root/'run_python.zip'),'launcher_sha256':sha(root/'process_python_talker')}
    (root/'run_python.json').write_text(json.dumps(value,indent=2)+'\n')


def prepare(root,expected):
    if (root/'owner').read_text()!=f'MDDS_RUN_OWNER RUN_ID={root.name} LABEL=ros_broker\n':raise ValueError('wrong Python demo owner')
    if sha(root/'run_python.json')!=expected:raise ValueError('Python demo manifest differs')
    value=json.loads((root/'run_python.json').read_bytes());site=root/'python'
    extract_wheel(root/'run_python.zip',site,value['archive_sha256'])
    for item in value['files']:
        if not (site/item['path']).resolve().is_relative_to(site.resolve()) or sha(site/item['path'])!=item['sha256']:
            raise ValueError('Python demo file differs')
    if sha(root/'execution_prefix/lib/demo_nodes_py/talker')!=value['launcher_sha256']:raise ValueError('Python demo launcher differs')
    result={'manifest_sha256':expected,'files':len(value['files']),'run_id':root.name}
    (root/'run_python_ready.json').write_text(json.dumps(result)+'\n')
    print('RUN_PYTHON_READY '+expected,flush=True)


if __name__=='__main__':
    if sys.argv[1]=='pack':pack(Path(sys.argv[2]))
    else:prepare(Path(sys.argv[2]),sys.argv[3])
