"""Rich victim/survivor entities with exact peer messages and RPCs."""
import json
from abrupt_graph_contract import base,messages

class Entities:
    def __init__(self,node,run,nonce,role,kind):
        from std_msgs.msg import String
        from example_interfaces.srv import AddTwoInts
        self.node,self.run,self.nonce,self.role,self.kind=node,run,nonce,role,kind
        self.peer='B' if role=='A' else 'A';self.String,self.Service=String,AddTwoInts
        self.received={};self.served=[];self.futures={};self.started=set();self.actions=[]
        own=base(run,role,kind);self.pub=node.create_publisher(String,own+'/out',32);self.clients={}
        for target in (('survivor','victim') if kind=='survivor' else ('survivor',)):
            self.received[target]=[]
            def receive(message,target=target):
                expected=messages(run,nonce,self.peer,target,1)
                if target=='survivor' and kind=='survivor':expected+=messages(run,nonce,self.peer,target,2)
                values=self.received[target]
                if len(values)>=len(expected) or message.data!=expected[len(values)]:raise ValueError('abrupt payload identity/order differs')
                values.append(message.data)
                print('ABRUPT_RX '+json.dumps({'run_id':run,'nonce':nonce,'role':role,'owner':kind,'source':target,'data':message.data}),flush=True)
            node.create_subscription(String,base(run,self.peer,target)+'/out',receive,32)
            self.clients[target]=node.create_client(AddTwoInts,base(run,self.peer,target)+'/serve')
        def serve(request,response):
            allowed=(1111,2222,3333) if kind=='survivor' else (1112,)
            if request.a!=int(nonce[:7],16)+(1 if self.peer=='A' else 2) or request.b not in allowed or request.b in self.served:raise ValueError('abrupt service request differs or duplicated')
            response.sum=request.a+request.b;self.served.append(request.b)
            print('ABRUPT_SERVICE_RX '+json.dumps({'run_id':run,'nonce':nonce,'role':role,'owner':kind,'a':request.a,'b':request.b,'sum':response.sum}),flush=True)
            return response
        node.create_service(AddTwoInts,own+'/serve',serve)
        if kind=='victim':
            from rclpy.action import ActionServer,ActionClient
            from example_interfaces.action import Fibonacci
            def execute(handle):result=Fibonacci.Result();result.sequence=[0,1];handle.succeed();return result
            self.actions=[ActionServer(node,Fibonacci,own+'/action',execute),ActionClient(node,Fibonacci,base(run,self.peer,kind)+'/action')]
    def start(self,stage):
        if stage in self.started:return True
        targets=('survivor','victim') if self.kind=='survivor' and stage==1 else ('survivor',)
        if self.pub.get_subscription_count()!=(2 if self.kind=='survivor' and stage==1 else 1) or not all(self.clients[t].service_is_ready() for t in targets):return False
        for value in messages(self.run,self.nonce,self.role,self.kind,stage):
            message=self.String();message.data=value;self.pub.publish(message)
        for target in targets:
            request=self.Service.Request();request.a=int(self.nonce[:7],16)+(1 if self.role=='A' else 2)
            request.b=2222 if self.kind=='victim' else (3333 if stage==2 else (1111 if target=='survivor' else 1112))
            self.futures[stage,target]=(self.clients[target].call_async(request),request.a+request.b)
        self.started.add(stage);return True
    def complete(self,stage):
        if stage not in self.started:return False
        if self.kind=='survivor':
            if self.received['victim']!=messages(self.run,self.nonce,self.peer,'victim',1):return False
            expected=messages(self.run,self.nonce,self.peer,'survivor',1)+(messages(self.run,self.nonce,self.peer,'survivor',2) if stage==2 else [])
            required={1111,2222}|({3333} if stage==2 else set())
        else:expected=messages(self.run,self.nonce,self.peer,'survivor',1);required={1112}
        if self.received['survivor']!=expected or not required.issubset(self.served):return False
        for (number,target),(future,wanted) in self.futures.items():
            if number!=stage:continue
            if not future.done():return False
            if future.result().sum!=wanted:raise ValueError('abrupt service response differs')
        return True
    def summary(self,stage):
        return {'received':{k:list(v) for k,v in self.received.items()},'served':sorted(self.served),
                'results':{t:f.result().sum for (s,t),(f,wanted) in self.futures.items() if s==stage}}
    def close(self):
        for action in self.actions:action.destroy()
        self.node.destroy_node()
