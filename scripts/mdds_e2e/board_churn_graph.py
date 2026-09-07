"""Drive graph churn with causal peer barriers over real ROS transport."""
import json
import time
from churn_graph_contract import ROUNDS,scope,base,node_id,payload,validate_snapshot,validate_history

class ChurnGraphSource:
    def __init__(self,root,run,nonce,role,observer,executor):
        self.root,self.run,self.nonce,self.role,self.observer,self.executor=root,run,nonce,role,observer,executor
        self.peer='B' if role=='A' else 'A';self.phase=0;self.peer_phase=-1;self.records={};self.results=[];self.advances=[]
        self.ready=False;self.next_control=0;self.closed=False;self.finished=False;self.attempts=0;self.pending=None
    def marker(self,name):
        p=self.root/name
        if not p.exists():return False
        if p.read_text().strip()!=self.nonce:raise ValueError('churn barrier differs: '+name)
        return True
    def make(self,kind):
        import rclpy
        from rclpy.context import Context
        from rclpy.node import Node
        from rclpy.executors import SingleThreadedExecutor
        from rclpy.signals import SignalHandlerOptions
        from std_msgs.msg import String
        from example_interfaces.srv import AddTwoInts
        self.String,self.Service=String,AddTwoInts
        context=self.observer.context;executor=self.executor
        if kind=='transient':
            context=Context();rclpy.init(context=context,signal_handler_options=SignalHandlerOptions.NO)
            executor=SingleThreadedExecutor(context=context)
        fqn=node_id(self.run,self.role,kind);ns,name=fqn.rsplit('/',1)
        node=Node(name,namespace=ns,context=context,start_parameter_services=False,enable_rosout=False)
        executor.add_node(node)
        r={'kind':kind,'node':node,'executor':executor,'context':context,'created':self.phase,'received':[],'served':[],'sent':-1,'future':None,'actions':[]}
        self.records[kind]=r;own=base(self.run,self.role,kind);peer=base(self.run,self.peer,kind)
        r['pub']=node.create_publisher(String,own+'/out',64)
        def receive(message,r=r):
            index=len(r['received']) if r['kind']=='survivor' else r['created']
            if r['kind']=='transient' and r['received']:raise ValueError('duplicate transient payload')
            if message.data!=payload(self.run,self.nonce,self.peer,r['kind'],index):raise ValueError('churn payload identity/order differs')
            r['received'].append(message.data)
            print('CHURN_RX '+json.dumps({'role':self.role,'kind':r['kind'],'index':index,'payload':message.data}),flush=True)
        node.create_subscription(String,peer+'/out',receive,64)
        def serve(request,response,r=r):
            index=len(r['served']) if r['kind']=='survivor' else r['created']
            if r['kind']=='transient' and r['served']:raise ValueError('duplicate transient request')
            if request.a!=int(self.nonce[:7],16)+(1 if self.peer=='A' else 2) or request.b!=1000+index:raise ValueError('churn service identity/order differs')
            response.sum=request.a+request.b;r['served'].append(index)
            print('CHURN_SERVICE_RX '+json.dumps({'role':self.role,'kind':r['kind'],'index':index,'a':request.a,'b':request.b,'sum':response.sum}),flush=True)
            return response
        node.create_service(AddTwoInts,own+'/serve',serve);r['client']=node.create_client(AddTwoInts,peer+'/serve')
        if kind=='survivor':
            self.control=node.create_publisher(String,own+'/control',64)
            def control(message):
                v=json.loads(message.data);index=v['index']
                if v!={'run_id':self.run,'nonce':self.nonce,'role':self.peer,'index':index} or type(index) is not int or not 0<=index<ROUNDS*2:raise ValueError('churn peer control differs')
                if index<=self.peer_phase:return
                if index!=self.peer_phase+1:raise ValueError('churn peer skipped a phase')
                self.peer_phase=index;print('CHURN_CONTROL_RX '+json.dumps(v),flush=True)
            node.create_subscription(String,peer+'/control',control,64)
        else:
            from rclpy.action import ActionServer,ActionClient
            from example_interfaces.action import Fibonacci
            def action(handle):result=Fibonacci.Result();result.sequence=[0,1];handle.succeed();return result
            r['actions']=[ActionServer(node,Fibonacci,own+'/action',action),ActionClient(node,Fibonacci,peer+'/action')]
    def retire(self,kind):
        r=self.records.pop(kind)
        for action in r['actions']:action.destroy()
        r['executor'].remove_node(r['node']);r['node'].destroy_node()
        if kind=='transient':r['executor'].shutdown();r['context'].shutdown();r['context'].destroy()
    def close(self):
        for kind in ('transient','survivor'):
            if kind in self.records:self.retire(kind)
    def snapshot(self):
        from rclpy.action.graph import get_action_server_names_and_types_by_node,get_action_client_names_and_types_by_node
        n=self.observer;ns=scope(self.run)
        value={'index':self.phase,'nodes':sorted(s.rstrip('/')+'/'+name for name,s in n.get_node_names_and_namespaces() if s==ns or s.startswith(ns+'/')),
               'topics':{name:sorted(types) for name,types in n.get_topic_names_and_types() if name.startswith(ns+'/')},
               'services':{name:sorted(types) for name,types in n.get_service_names_and_types() if name.startswith(ns+'/')},'by_node':{},'gids':{},
               'parameter_event_owners':sorted(ep.node_namespace.rstrip('/')+'/'+ep.node_name for ep in n.get_publishers_info_by_topic('/parameter_events') if ep.node_namespace==ns or ep.node_namespace.startswith(ns+'/'))}
        for fqn in value['nodes']:
            space,name=fqn.rsplit('/',1)
            getters={'publishers':n.get_publisher_names_and_types_by_node,'subscriptions':n.get_subscriber_names_and_types_by_node,'services':n.get_service_names_and_types_by_node,'clients':n.get_client_names_and_types_by_node}
            view={kind:{topic:sorted(types) for topic,types in getter(name,space)} for kind,getter in getters.items()}
            view['action_servers']=dict(get_action_server_names_and_types_by_node(n,name,space));view['action_clients']=dict(get_action_client_names_and_types_by_node(n,name,space));value['by_node'][fqn]=view
        for role in ('A','B'):
            for kind in (('survivor','transient') if self.phase%2==0 else ('survivor',)):
                infos=n.get_publishers_info_by_topic(base(self.run,role,kind)+'/out');fqn=node_id(self.run,role,kind)
                if len(infos)!=1 or infos[0].node_namespace.rstrip('/')+'/'+infos[0].node_name!=fqn or infos[0].topic_type!='std_msgs/msg/String':raise ValueError('churn publisher attribution differs')
                value['gids'][role+':'+kind]=list(infos[0].endpoint_gid)
        validate_snapshot(value,self.run,self.phase);return value
    def tick(self):
        if self.finished:return
        if not self.records and not self.closed:
            if not self.marker('hidden_source.go'):return
            self.make('survivor');self.make('transient')
        if 'transient' in self.records:self.records['transient']['executor'].spin_once(timeout_sec=0)
        if self.phase==ROUNDS*2:
            if self.marker('hidden_cli.go') and not (self.root/'hidden_cli.done').exists():(self.root/'hidden_cli.done').write_text(self.nonce+'\n')
            if not self.marker('hidden_source.stop'):return
            if not self.closed:self.retire('survivor');self.closed=True
            ns=scope(self.run);n=self.observer
            empty={'nodes':[s.rstrip('/')+'/'+name for name,s in n.get_node_names_and_namespaces() if s==ns or s.startswith(ns+'/')],
                   'topics':{name:t for name,t in n.get_topic_names_and_types() if name.startswith(ns+'/')},'services':{name:t for name,t in n.get_service_names_and_types() if name.startswith(ns+'/')},
                   'parameter_event_owners':[ep.node_namespace.rstrip('/')+'/'+ep.node_name for ep in n.get_publishers_info_by_topic('/parameter_events') if ep.node_namespace==ns or ep.node_namespace.startswith(ns+'/')]}
            if empty!={'nodes':[],'topics':{},'services':{},'parameter_event_owners':[]}:return
            value={'run_id':self.run,'nonce':self.nonce,'role':self.role,'rounds':ROUNDS,'query_attempts':self.attempts,'phases':self.results,'advances':self.advances,'final':empty}
            validate_history([v['snapshot'] for v in self.results],self.run)
            (self.root/'churn_graph.json').write_text(json.dumps(value)+'\n');print('CHURN_GRAPH_RESULT '+json.dumps(value),flush=True)
            (self.root/'hidden_source.done').write_text(self.nonce+'\n');self.finished=True;return
        if self.ready:
            if time.monotonic()>=self.next_control:
                message=self.String();message.data=json.dumps({'run_id':self.run,'nonce':self.nonce,'role':self.role,'index':self.phase});self.control.publish(message);self.next_control=time.monotonic()+.05
            if self.peer_phase<self.phase:return
            value={'index':self.phase,'peer_ready':self.peer_phase};self.advances.append(value);print('CHURN_ADVANCE '+json.dumps(value),flush=True)
            if self.phase%2==0:self.retire('transient')
            self.phase+=1;self.ready=False
            if self.phase==ROUNDS*2:
                summary={'run_id':self.run,'nonce':self.nonce,'role':self.role,'phases':self.phase}
                (self.root/'hidden_source.json').write_text(json.dumps(summary)+'\n');print('CHURN_WORK_DONE '+json.dumps(summary),flush=True)
            elif self.phase%2==0:self.make('transient')
            return
        self.attempts+=1
        from rclpy.node import NodeNameNonExistentError
        try:snapshot=self.snapshot()
        except (ValueError,RuntimeError,NodeNameNonExistentError) as error:
            message=str(error)
            if (self.phase,message)!=self.pending:self.pending=(self.phase,message);print('CHURN_PENDING '+json.dumps({'index':self.phase,'error':message}),flush=True)
            return
        for r in self.records.values():
            if r['sent']==self.phase:continue
            if r['pub'].get_subscription_count()!=1 or not r['client'].wait_for_service(timeout_sec=0):return
            message=self.String();message.data=payload(self.run,self.nonce,self.role,r['kind'],self.phase);r['pub'].publish(message)
            request=self.Service.Request();request.a=int(self.nonce[:7],16)+(1 if self.role=='A' else 2);request.b=1000+self.phase
            r['future']=r['client'].call_async(request);r['sent']=self.phase
        data={}
        for kind,r in self.records.items():
            wanted=payload(self.run,self.nonce,self.peer,kind,self.phase)
            if wanted not in r['received'] or self.phase not in r['served'] or not r['future'].done():return
            result=r['future'].result().sum
            if result!=int(self.nonce[:7],16)+(1 if self.role=='A' else 2)+1000+self.phase:raise ValueError('churn service response differs')
            data[kind]={'received':wanted,'sum':result}
        value={'index':self.phase,'snapshot':snapshot,'data':data};self.results.append(value);print('CHURN_PHASE '+json.dumps(value),flush=True)
        self.ready=True;self.next_control=0
