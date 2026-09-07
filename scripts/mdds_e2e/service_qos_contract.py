"""Exact two-board service QoS availability contract."""
import json


def service(run,role,kind): return '/service_qos_'+run+'/'+role+'/'+kind


def calls(nonce,role):
    return [{'kind':kind,'a':int(nonce[:7],16)+(1 if role=='A' else 2),'b':b,
             'sum':int(nonce[:7],16)+(1 if role=='A' else 2)+b}
            for kind,b in (('reliable',11),('best_effort',22))]


def expected(run,nonce,role):
    peer='B' if role=='A' else 'A';kinds=('reliable','best_effort');type_=['example_interfaces/srv/AddTwoInts']
    return {'run_id':run,'nonce':nonce,'role':role,'passed':True,
            'services':{service(run,peer,k):type_ for k in kinds},
            'clients':{service(run,role,k):type_ for k in kinds},
            'nonowners':{name+'_'+peer:{'services':{},'clients':{}} for name in ('beta','duplicate')},
            'counts':{service(run,peer,k):{'servers':1,'clients':2} for k in kinds},
            'ready_checks':[{'good_reliable':True,'good_best_effort':True,'bad_request':False,'bad_response':False} for _ in range(5)],
            'calls':calls(nonce,role)}


def validate(value,run,nonce,role):
    if json.dumps(value,sort_keys=True)!=json.dumps(expected(run,nonce,role),sort_keys=True):
        raise ValueError('service QoS ownership/count/availability/positive control differs')
