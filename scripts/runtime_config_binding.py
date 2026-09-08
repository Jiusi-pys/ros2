"""Bind a ROS-only Python loader policy without changing the compiled archive."""
import copy
import hashlib
import json
from pathlib import Path
import re
import sys


def bind(document, bootstrap_sha, controller_sha):
    if document.get('schema') != 'ros2-ohos-release-provenance-v1':
        raise ValueError('unexpected provenance schema')
    if not all(re.fullmatch('[0-9a-f]{64}', value) for value in (bootstrap_sha, controller_sha)):
        raise ValueError('invalid runtime configuration digest')
    result = copy.deepcopy(document)
    result.pop('record_sha256', None)
    result['runtime_configuration'] = {
        'python_bootstrap_sha256': bootstrap_sha,
        'python_bootstrap_path': '/data/local/tmp/ros2-core-config/python-' + bootstrap_sha + '/sitecustomize.py',
        'deployment_controller_sha256': controller_sha,
        'python_dlopen_policy': 'OHOS ROS processes: preserve flags and add RTLD_GLOBAL',
    }
    canonical = (json.dumps(result, sort_keys=True, separators=(',', ':'), ensure_ascii=False) + '\n').encode()
    result['record_sha256'] = hashlib.sha256(canonical).hexdigest()
    return result


if __name__ == '__main__':
    path = Path(sys.argv[1])
    result = bind(json.loads(path.read_text()), sys.argv[2], sys.argv[3])
    temporary = path.with_name(path.name + '.runtime-configured')
    with temporary.open('x', encoding='utf-8', newline='\n') as stream:
        stream.write(json.dumps(result, indent=2, sort_keys=True) + '\n')
    temporary.replace(path)
