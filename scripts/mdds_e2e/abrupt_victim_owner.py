"""Exact process identity required before signaling a spawned victim."""
def owned_victim(value,root,argv,pid,start):
    return (type(pid) is int and pid>1 and value.get('pid')==pid and value.get('start')==start
            and isinstance(start,str) and start.isdecimal() and value.get('state') not in (None,'Z')
            and value.get('argv')==argv and value.get('broker_root')==root+'/brokers'
            and sorted(value.get('libraries',[]))==[root+'/lib/libmdds.so',root+'/lib/librmw_mdds.so'])
