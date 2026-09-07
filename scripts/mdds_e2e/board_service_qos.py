"""Exercise two-direction service matching through real peer RMW endpoints."""
import json
import time
from service_qos_contract import calls,service,validate


class ServiceQoSProbe:
    def __init__(self,root,run,nonce,role,node):
        from example_interfaces.srv import AddTwoInts
        from rclpy.qos import QoSProfile,ReliabilityPolicy
        self.root,self.run,self.nonce,self.role,self.node=root,run,nonce,role,node
        self.peer='B' if role=='A' else 'A';self.done=False;self.checks=[];self.futures={};self.served=[]
        self.deadline=time.monotonic()+45;self.next_check=0;self.AddTwoInts=AddTwoInts
        qos={'reliable':QoSProfile(depth=10,reliability=ReliabilityPolicy.RELIABLE),
             'best_effort':QoSProfile(depth=10,reliability=ReliabilityPolicy.BEST_EFFORT)}
        self.servers=[]
        for kind in qos:
            def callback(request,response,kind=kind):
                wanted=next(c for c in calls(nonce,self.peer) if c['kind']==kind)
                if request.a!=wanted['a'] or request.b!=wanted['b'] or any(v['kind']==kind for v in self.served):raise ValueError('unexpected service QoS request')
                response.sum=request.a+request.b;self.served.append(wanted)
                value={'run_id':run,'nonce':nonce,'role':role,'served':sorted(self.served,key=lambda v:v['kind'])}
                tmp=root/'service_qos_server.pending';tmp.write_text(json.dumps(value)+'\n');tmp.replace(root/'service_qos_server.json')
                print('SERVICE_QOS_SERVER '+json.dumps(value),flush=True)
                return response
            self.servers.append(node.create_service(AddTwoInts,service(run,role,kind),callback,qos_profile=qos[kind]))
        self.clients={
            'good_reliable':node.create_client(AddTwoInts,service(run,self.peer,'reliable'),qos_profile=qos['reliable']),
            'good_best_effort':node.create_client(AddTwoInts,service(run,self.peer,'best_effort'),qos_profile=qos['best_effort']),
            'bad_request':node.create_client(AddTwoInts,service(run,self.peer,'reliable'),qos_profile=qos['best_effort']),
            'bad_response':node.create_client(AddTwoInts,service(run,self.peer,'best_effort'),qos_profile=qos['reliable'])}

    def tick(self):
        if self.done:return
        now=time.monotonic()
        if now>self.deadline:raise RuntimeError('service QoS probe did not finish')
        counts={service(self.run,self.peer,k):{'servers':self.node.count_services(service(self.run,self.peer,k)),
                'clients':self.node.count_clients(service(self.run,self.peer,k))} for k in ('reliable','best_effort')}
        ready={name:client.service_is_ready() for name,client in self.clients.items()}
        if not ready['good_reliable'] or not ready['good_best_effort'] or any(v!={'servers':1,'clients':2} for v in counts.values()):return
        if len(self.checks)<5:
            if now<self.next_check:return
            self.checks.append(ready);self.next_check=now+.1
            if ready['bad_request'] or ready['bad_response']:
                print('SERVICE_QOS_INVALID_READY '+json.dumps(ready),flush=True)
                raise RuntimeError('one-direction service match incorrectly reported available')
            return
        if not self.futures:
            for row in calls(self.nonce,self.role):
                request=self.AddTwoInts.Request();request.a=row['a'];request.b=row['b']
                self.futures[row['kind']]=self.clients['good_'+row['kind']].call_async(request)
            return
        if not all(f.done() for f in self.futures.values()):return
        prefix='/service_qos_'+self.run+'/'
        services=dict((name,types) for name,types in self.node.get_service_names_and_types_by_node('alpha_'+self.peer,'/ros_broker_'+self.run) if name.startswith(prefix))
        clients=dict((name,types) for name,types in self.node.get_client_names_and_types_by_node('alpha_'+self.peer,'/ros_broker_'+self.run) if name.startswith(prefix))
        nonowners={}
        for name in ('beta_'+self.peer,'duplicate_'+self.peer):
            nonowners[name]={'services':dict((n,t) for n,t in self.node.get_service_names_and_types_by_node(name,'/ros_broker_'+self.run) if n.startswith(prefix)),
                             'clients':dict((n,t) for n,t in self.node.get_client_names_and_types_by_node(name,'/ros_broker_'+self.run) if n.startswith(prefix))}
        results=[]
        for row in calls(self.nonce,self.role):results.append({**row,'sum':self.futures[row['kind']].result().sum})
        value={'run_id':self.run,'nonce':self.nonce,'role':self.role,'passed':True,'services':services,'clients':clients,'nonowners':nonowners,
               'counts':counts,'ready_checks':self.checks,'calls':results}
        validate(value,self.run,self.nonce,self.role)
        (self.root/'service_qos.json').write_text(json.dumps(value)+'\n');self.done=True
        print('SERVICE_QOS_RESULT '+json.dumps(value),flush=True)
