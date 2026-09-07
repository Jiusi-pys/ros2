import json
import re
import sys
from pathlib import Path


def paths(files):
    if not isinstance(files,list) or not 4<=len(files)<=12:raise ValueError('unexpected bag file count')
    result=[]
    for item in files:
        value=item['path']
        if not re.fullmatch(r'bags/(sqlite3|mcap)/[A-Za-z0-9_.-]+',value) or value.split('/')[-1] in ('.','..') or not re.fullmatch('[0-9a-f]{64}',item['sha256']) or not 0<=item['size']<=16*1024*1024:raise ValueError('invalid bag file manifest')
        if value in result:raise ValueError('duplicate bag path')
        result.append(value)
    return result


if __name__=='__main__':
    for value in paths(json.loads(Path(sys.argv[1]).read_text(encoding='utf-8'))):print(value)
