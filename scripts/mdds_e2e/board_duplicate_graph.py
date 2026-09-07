"""Exercise identical node names across four real MDDS participants."""
import json
from duplicate_graph_contract import namespace,topic,validate,payload


class DuplicateGraphSource:
    def __init__(self,root,run,nonce,role,node):
        self.root,self.run,self.nonce,self.role,self.observer=root,run,nonce,role,node
        self.records=[];self.phase=0;self.initial=None;self.finished=False;self.last_error=None;self.results=[]

    def marker(self,name):
        path=self.root/name
        if not path.exists():return False
        if path.read_text().strip()!=self.nonce:raise ValueError('duplicate phase barrier differs: '+name)
        return True

    def create(self):
        import rclpy
        from rclpy.context import Context
        from rclpy.node import Node
        from rclpy.executors import SingleThreadedExecutor
        from rclpy.signals import SignalHandlerOptions
        from std_msgs.msg import String
        from example_interfaces.srv import AddTwoInts
        self.message_type,self.service_type=String,AddTwoInts
        other='B' if self.role=='A' else 'A'
        for index in (0,1):
            context=Context();rclpy.init(context=context,signal_handler_options=SignalHandlerOptions.NO)
            node=Node('same',namespace=namespace(self.run),context=context,start_parameter_services=False,enable_rosout=False)
            executor=SingleThreadedExecutor(context=context);executor.add_node(node)
            key=self.role+str(index);peer=other+str(index)
            record={'context':context,'node':node,'executor':executor,'key':key,'peer':peer,'received':[],'served':[],'sent':False,'future':None}
            self.records.append(record)
            record['pub']=node.create_publisher(String,topic(self.run,key),10)
            def receive(message,r=record):
                allowed=[payload(self.run,self.nonce,r['peer'],p,i) for p in (1,2) for i in range(3)]
                if message.data not in allowed or message.data in r['received']:raise ValueError('unexpected/duplicate same-name payload')
                r['received'].append(message.data)
            node.create_subscription(String,topic(self.run,peer),receive,10)
            def serve(request,response,r=record):
                if request.a!=int(self.nonce[:7],16) or request.b not in (101,102):raise ValueError('same-name RPC request differs')
                response.sum=request.a+request.b;r['served'].append(request.b)
                print('DUPLICATE_SERVICE_RX '+json.dumps({'key':r['key'],'nonce':self.nonce,'a':request.a,'b':request.b,'sum':response.sum}),flush=True)
                return response
            node.create_service(AddTwoInts,topic(self.run,key)+'/serve',serve)
            record['client']=node.create_client(AddTwoInts,topic(self.run,peer)+'/serve')
        if len({id(r['context']) for r in self.records})!=2:raise ValueError('contexts shared')
        self.phase=1

    def snapshot(self):
        node=self.observer;ns=namespace(self.run);prefix=ns+'/'
        value={'phase':self.phase,'nodes':sorted([list(v) for v in node.get_node_names_and_namespaces() if v[1]==ns]),'gids':{}}
        for kind,getter in [('publishers',node.get_publisher_names_and_types_by_node),('subscriptions',node.get_subscriber_names_and_types_by_node),('services',node.get_service_names_and_types_by_node),('clients',node.get_client_names_and_types_by_node)]:
            value[kind]={n:sorted(t) for n,t in getter('same',ns) if n.startswith(prefix)}
        for role in ('A','B'):
            for i in ((0,1) if self.phase==1 else (0,)):
                key=role+str(i);entries=node.get_publishers_info_by_topic(topic(self.run,key))
                if len(entries)!=1 or entries[0].node_name!='same' or entries[0].node_namespace!=ns:raise ValueError('same-name endpoint attribution differs')
                value['gids'][key]=list(entries[0].endpoint_gid)
        validate(value,self.run,self.phase,self.initial)
        return value

    def retire(self,record):
        record['executor'].remove_node(record['node']);record['node'].destroy_node()
        record['executor'].shutdown();record['context'].shutdown();record['context'].destroy();self.records.remove(record)

    def tick(self):
        if self.finished:return
        if self.phase==0:
            if not self.marker('hidden_source.go'):return
            self.create()
        for record in self.records:record['executor'].spin_once(timeout_sec=0)
        if self.phase==1 and self.initial is not None:
            if not self.marker('hidden_cli.go'):return
            self.retire(self.records[1]);self.phase=2
            for record in self.records:record['sent']=False;record['future']=None
        if self.phase==2 and (self.root/'duplicate_survivor.json').exists():
            if not self.marker('hidden_source.stop'):return
            for record in list(self.records):self.retire(record)
            print('DUPLICATE_GRAPH_RESULT '+json.dumps(self.results),flush=True)
            self.finished=True;(self.root/'hidden_source.done').write_text(self.nonce+'\n');return
        try:snapshot=self.snapshot()
        except (ValueError,RuntimeError) as error:
            if str(error)!=self.last_error:
                self.last_error=str(error);print('DUPLICATE_GRAPH_PENDING '+self.last_error,flush=True)
                (self.root/'duplicate.pending').write_text(self.last_error)
            return
        for record in self.records:
            if not record['sent']:
                if record['pub'].get_subscription_count()!=1 or not record['client'].wait_for_service(timeout_sec=0):return
                for index in range(3):
                    message=self.message_type();message.data=payload(self.run,self.nonce,record['key'],self.phase,index);record['pub'].publish(message)
                request=self.service_type.Request();request.a=int(self.nonce[:7],16);request.b=100+self.phase
                record['future']=record['client'].call_async(request);record['sent']=True
        data={}
        for record in self.records:
            expected=[payload(self.run,self.nonce,record['peer'],self.phase,i) for i in range(3)]
            received=[v for v in record['received'] if v in expected]
            if sorted(received)!=sorted(expected) or not record['future'].done() or 100+self.phase not in record['served']:return
            result=record['future'].result().sum
            if result!=int(self.nonce[:7],16)+100+self.phase:raise ValueError('same-name RPC response differs')
            data[record['key']]={'received':received,'service_sum':result,'served':list(record['served'])}
        value={'run_id':self.run,'nonce':self.nonce,'role':self.role,'snapshot':snapshot,'data':data}
        self.results.append(value)
        name='hidden_source.json' if self.phase==1 else 'duplicate_survivor.json'
        (self.root/name).write_text(json.dumps(value)+'\n');print('DUPLICATE_GRAPH_PHASE '+json.dumps(value),flush=True)
        if self.phase==1:self.initial=snapshot
        else:(self.root/'hidden_cli.done').write_text(self.nonce+'\n')
