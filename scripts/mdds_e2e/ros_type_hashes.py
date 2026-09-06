"""Freeze expected graph hashes from the generated ROS type descriptions."""
import hashlib
import json
from pathlib import Path
import re
import sys


def generated_hashes(workspace):
    sources = {
        'std_msgs/msg/String': 'build_ohos/std_msgs/rosidl_generator_type_description/std_msgs/msg/String.json',
        'example_interfaces/srv/AddTwoInts_Request': 'build_ohos/example_interfaces/rosidl_generator_type_description/example_interfaces/srv/AddTwoInts_Request.json',
        'example_interfaces/srv/AddTwoInts_Response': 'build_ohos/example_interfaces/rosidl_generator_type_description/example_interfaces/srv/AddTwoInts_Response.json',
    }
    hashes, records = {}, {}
    for name, relative in sources.items():
        raw = (Path(workspace) / relative).read_bytes()
        entries = [v['hash_string'] for v in json.loads(raw)['type_hashes'] if v['type_name'] == name]
        if len(entries) != 1 or not re.fullmatch(r'RIHS01_[0-9a-f]{64}', entries[0]):
            raise ValueError('missing or invalid generated type hash: ' + name)
        hashes[name] = entries[0]
        records[name] = {'path': relative, 'sha256': hashlib.sha256(raw).hexdigest()}
    return {'hashes': hashes, 'sources': records}


if __name__ == '__main__':
    with Path(sys.argv[2]).open('x', encoding='utf-8') as output:
        json.dump(generated_hashes(sys.argv[1]), output, indent=2)
        output.write('\n')
