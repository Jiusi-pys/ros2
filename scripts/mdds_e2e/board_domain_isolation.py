"""Run two enabled ROS domains with identical endpoint names and exact controls."""
import json
import time
from abrupt_graph_contract import write_json
from cycle_graph_contract import collect
from domain_isolation_contract import DOMAINS,scope,payloads,validate_snapshot,validate_counts,validate_data

class DomainIsolationProbe:
    def __init__(self,root,run,nonce,role,observer):
        self.root,self.run,self.nonce,self.role,self.observer=root,run,nonce,role,observer
        self.peer='B' if role=='A' else 'A';self.records={};self.sent=False;self.started=None;self.done=False;self.closed=False;self.pending=None
        self.hashes=json.loads((root/'late_graph_hashes.json').read_bytes())['hashes']
    def marker(self,name):
        p=self.root/name
        if not p.exists():return False
        if p.read_text().strip()!=self.nonce:raise ValueError('domain barrier differs')
        return True
    def create(self):
        import rclpy
        from rclpy.context import Context
        from rclpy.node import Node
        from rclpy.executors import SingleThreadedExecutor
        from rclpy.signals import SignalHandlerOptions
        from std_msgs.msg import String
        from example_interfaces.srv import AddTwoInts
        self.String,self.Service=String,AddTwoInts;ns=scope(self.run)
        for domain in DOMAINS:
            ctx=Context();rclpy.init(args=['--ros-args','--enclave',f'/isolation/d{domain}/'+self.role],context=ctx,domain_id=domain,signal_handler_options=SignalHandlerOptions.NO)
            if ctx.get_domain_id()!=domain:raise ValueError('actual context domain differs')
            node=Node('domain_'+self.role,namespace=ns,context=ctx,start_parameter_services=False,enable_rosout=False)
            executor=SingleThreadedExecutor(context=ctx);executor.add_node(node)
            r={'context':ctx,'node':node,'executor':executor,'received':[],'served':None,'future':None};self.records[domain]=r
            r['pub']=node.create_publisher(String,ns+'/samples',10)
            def receive(message,domain=domain,r=r):
                expected=payloads(self.run,self.nonce,domain,'A')+payloads(self.run,self.nonce,domain,'B')
                if message.data not in expected or message.data in r['received']:raise ValueError('foreign domain or duplicate sample')
                r['received'].append(message.data);print('DOMAIN_RX '+json.dumps({'run_id':self.run,'nonce':self.nonce,'role':self.role,'domain':domain,'data':message.data}),flush=True)
            r['sub']=node.create_subscription(String,ns+'/samples',receive,10)
            def serve(request,response,domain=domain,r=r):
                expected=int(self.nonce[:7],16)+domain*10+(1 if self.peer=='A' else 2)
                if r['served'] is not None or request.a!=expected or request.b!=303:raise ValueError('foreign domain or repeated RPC')
                response.sum=request.a+request.b;r['served']={'run_id':self.run,'nonce':self.nonce,'role':self.role,'domain':domain,'a':request.a,'b':request.b,'sum':response.sum}
                print('DOMAIN_SERVER '+json.dumps(r['served']),flush=True);return response
            node.create_service(AddTwoInts,ns+'/'+self.role+'/serve',serve);r['client']=node.create_client(AddTwoInts,ns+'/'+self.peer+'/serve')
    def close(self):
        if self.closed:return
        for r in self.records.values():r['executor'].remove_node(r['node']);r['node'].destroy_node();r['executor'].shutdown();r['context'].try_shutdown();r['context'].destroy()
        self.closed=True
    def tick(self):
        if self.closed:return
        if not self.records:
            if not self.marker('hidden_source.go'):return
            self.create()
        for r in self.records.values():r['executor'].spin_once(timeout_sec=0)
        if self.done:
            if self.marker('hidden_source.stop'):self.close();(self.root/'hidden_source.done').write_text(self.nonce+'\n')
            return
        states={};ns=scope(self.run)
        try:
            for domain,r in self.records.items():
                node=r['node'];snapshot=collect(node,ns);validate_snapshot(snapshot,self.run,domain,self.hashes)
                if domain==176 and sorted(list(v) for v in node.get_node_names_and_namespaces_with_enclaves())!=snapshot['nodes']:raise ValueError('domain 176 leaked foreign global graph')
                counts={'publishers':node.count_publishers(ns+'/samples'),'subscriptions':node.count_subscribers(ns+'/samples'),'writer_matches':r['pub'].get_subscription_count(),'reader_matches':r['sub'].get_publisher_count(),
                        'servers':node.count_services(ns+'/'+self.peer+'/serve'),'clients':node.count_clients(ns+'/'+self.peer+'/serve')}
                validate_counts(counts)
                if not r['client'].service_is_ready():return
                states[str(domain)]={'requested_domain':domain,'actual_domain':r['context'].get_domain_id(),'snapshot':snapshot,'counts':counts}
        except ValueError as error:
            if str(error)!=self.pending:self.pending=str(error);print('DOMAIN_PENDING '+self.pending,flush=True)
            return
        if not (self.root/'hidden_source.json').exists():write_json(self.root,'hidden_source.json',{'run_id':self.run,'nonce':self.nonce,'role':self.role,'ready':True})
        if not self.marker('hidden_cli.go'):return
        if not self.sent:
            for domain,r in self.records.items():
                for value in payloads(self.run,self.nonce,domain,self.role):message=self.String();message.data=value;r['pub'].publish(message)
                request=self.Service.Request();request.a=int(self.nonce[:7],16)+domain*10+(1 if self.role=='A' else 2);request.b=303;r['future']=r['client'].call_async(request)
            self.sent=True;(self.root/'domain_isolation.sent').write_text(self.nonce+'\n')
        if not self.marker('domain_isolation.observe'):return
        if self.started is None:self.started=time.monotonic_ns()
        if time.monotonic_ns()-self.started<1_000_000_000:return
        for domain,r in self.records.items():
            if not r['future'].done() or r['served'] is None:return
            validate_data(r['received'],self.run,self.nonce,domain)
            expected=int(self.nonce[:7],16)+domain*10+(1 if self.role=='A' else 2)+303
            if r['future'].result().sum!=expected:raise ValueError('domain RPC response differs')
            states[str(domain)].update(received=list(r['received']),served=r['served'],rpc_sum=expected,sent=payloads(self.run,self.nonce,domain,self.role))
        value={'run_id':self.run,'nonce':self.nonce,'role':self.role,'domains':states,'observation_ns':time.monotonic_ns()-self.started}
        write_json(self.root,'domain_isolation.json',value);print('DOMAIN_ISOLATION_RESULT '+json.dumps(value),flush=True)
        (self.root/'hidden_cli.done').write_text(self.nonce+'\n');self.done=True
