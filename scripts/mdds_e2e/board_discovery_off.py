"""Exercise OFF and enabled contexts together over the same native broker."""
import json
import os
import time
from abrupt_graph_contract import write_json
from cycle_graph_contract import collect
from discovery_off_contract import scope,payloads,validate_snapshot,validate_counts,validate_data

class DiscoveryOffProbe:
    def __init__(self,root,run,nonce,role,observer):
        self.root,self.run,self.nonce,self.role,self.observer=root,run,nonce,role,observer
        self.contexts=[];self.executors=[];self.nodes=[];self.received={'off':[],'on':[]};self.served={};self.futures={};self.sent=False;self.started=None;self.done=False;self.closed=False
        self.hashes=json.loads((root/'late_graph_hashes.json').read_bytes())['hashes'];self.peer='B' if role=='A' else 'A';self.settings=[]
    def marker(self,name):
        p=self.root/name
        if not p.exists():return False
        if p.read_text().strip()!=self.nonce:raise ValueError('OFF test barrier differs')
        return True
    def create(self):
        import rclpy
        from rclpy.context import Context
        from rclpy.node import Node
        from rclpy.executors import SingleThreadedExecutor
        from rclpy.signals import SignalHandlerOptions
        from rclpy.utilities import get_rmw_implementation_identifier
        from std_msgs.msg import String
        from example_interfaces.srv import AddTwoInts
        self.String,self.Service=String,AddTwoInts;self.by_mode={};ns=scope(self.run)
        for mode,setting in (('off','OFF'),('on','SYSTEM_DEFAULT')):
            context=Context();previous=os.environ.get('ROS_AUTOMATIC_DISCOVERY_RANGE')
            try:
                os.environ['ROS_AUTOMATIC_DISCOVERY_RANGE']=setting
                rclpy.init(args=['--ros-args','--enclave','/discovery/'+self.role+'/'+mode],context=context,signal_handler_options=SignalHandlerOptions.NO)
            finally:
                if previous is None:os.environ.pop('ROS_AUTOMATIC_DISCOVERY_RANGE',None)
                else:os.environ['ROS_AUTOMATIC_DISCOVERY_RANGE']=previous
            self.contexts.append(context);executor=SingleThreadedExecutor(context=context);self.executors.append(executor)
            names=['off_'+self.role,'off_local_'+self.role] if mode=='off' else ['on_'+self.role]
            nodes=[]
            for name in names:
                node=Node(name,namespace=ns,context=context,start_parameter_services=False,enable_rosout=False);executor.add_node(node);self.nodes.append((executor,node));nodes.append(node)
            self.by_mode[mode]=nodes;self.settings.append({'mode':mode,'range':setting,'rmw':get_rmw_implementation_identifier()})
        off,local=self.by_mode['off'];on=self.by_mode['on'][0];self.pub={};self.sub={};self.clients={}
        for mode,publisher,subscriber in (('off',off,local),('on',on,on)):
            self.pub[mode]=publisher.create_publisher(String,ns+'/samples',10)
            def receive(message,mode=mode):
                allowed=payloads(self.run,self.nonce,self.role,'off') if mode=='off' else payloads(self.run,self.nonce,'A','on')+payloads(self.run,self.nonce,'B','on')
                if message.data not in allowed or message.data in self.received[mode]:raise ValueError('OFF boundary leaked or duplicated sample')
                self.received[mode].append(message.data);print('DISCOVERY_OFF_RX '+json.dumps({'run_id':self.run,'nonce':self.nonce,'role':self.role,'mode':mode,'data':message.data}),flush=True)
            self.sub[mode]=subscriber.create_subscription(String,ns+'/samples',receive,10)
        for mode,node in (('off',off),('on',on)):
            def serve(request,response,mode=mode):
                requester=self.role if mode=='off' else self.peer
                if mode in self.served or request.a!=int(self.nonce[:7],16)+(1 if requester=='A' else 2) or request.b!=(101 if mode=='off' else 202):raise ValueError('OFF/control RPC differs')
                response.sum=request.a+request.b;v={'run_id':self.run,'nonce':self.nonce,'role':self.role,'mode':mode,'a':request.a,'b':request.b,'sum':response.sum};self.served[mode]=v;print('DISCOVERY_OFF_SERVER '+json.dumps(v),flush=True);return response
            node.create_service(AddTwoInts,ns+'/'+mode+'_'+self.role+'/serve',serve)
        for key,node,target in [('off_local',local,'off_'+self.role),('off_on_local',local,'on_'+self.role),('off_on_remote',local,'on_'+self.peer),('on_remote',on,'on_'+self.peer),('on_off_local',on,'off_'+self.role),('on_off_remote',on,'off_'+self.peer)]:self.clients[key]=node.create_client(AddTwoInts,ns+'/'+target+'/serve')
    def close(self):
        if self.closed:return
        for executor,node in reversed(self.nodes):executor.remove_node(node);node.destroy_node()
        for executor in self.executors:executor.shutdown()
        for context in self.contexts:context.try_shutdown();context.destroy()
        self.closed=True
    def tick(self):
        if self.closed:return
        if not self.contexts:
            if not self.marker('hidden_source.go'):return
            self.create()
        for executor in self.executors:executor.spin_once(timeout_sec=0)
        if self.done:
            if self.marker('hidden_source.stop'):
                self.close();(self.root/'hidden_source.done').write_text(self.nonce+'\n')
            return
        off=self.by_mode['off'][0];on=self.by_mode['on'][0];ns=scope(self.run)
        # OFF must not discover even the pre-existing contexts in this process.
        all_off=sorted(list(v) for v in off.get_node_names_and_namespaces_with_enclaves())
        from discovery_off_contract import expected_nodes
        if all_off!=expected_nodes(self.run,self.role,'off'):raise ValueError('OFF context discovered nonlocal nodes')
        if any(name.startswith('off_') for name,space in on.get_node_names_and_namespaces() if space==ns):raise ValueError('enabled context discovered OFF nodes')
        ready={k:v.service_is_ready() for k,v in self.clients.items()}
        if any(ready[k] for k in ('off_on_local','off_on_remote','on_off_local','on_off_remote')):raise ValueError('service crossed the OFF boundary')
        counts={}
        for mode,node in (('off',off),('on',on)):
            counts.update({mode+'_publishers':node.count_publishers(ns+'/samples'),mode+'_subscriptions':node.count_subscribers(ns+'/samples'),mode+'_writer_matches':self.pub[mode].get_subscription_count(),mode+'_reader_matches':self.sub[mode].get_publisher_count()})
        try:
            validate_counts(counts)
            snapshots={mode:collect(node,ns) for mode,node in (('off',off),('on',on))}
            for mode,snapshot in snapshots.items():validate_snapshot(snapshot,self.run,self.role,mode,self.hashes)
        except ValueError as error:
            print('DISCOVERY_OFF_PENDING '+str(error),flush=True);return
        if not ready['off_local'] or not ready['on_remote']:return
        if not (self.root/'hidden_source.json').exists():write_json(self.root,'hidden_source.json',{'run_id':self.run,'nonce':self.nonce,'role':self.role,'ready':True})
        if not self.marker('hidden_cli.go'):return
        if not self.sent:
            for mode in ('off','on'):
                for value in payloads(self.run,self.nonce,self.role,mode):message=self.String();message.data=value;self.pub[mode].publish(message)
            for key,code in (('off_local',101),('on_remote',202)):
                request=self.Service.Request();request.a=int(self.nonce[:7],16)+(1 if self.role=='A' else 2);request.b=code;self.futures[key]=(self.clients[key].call_async(request),request.a+code)
            self.sent=True;(self.root/'discovery_off.sent').write_text(self.nonce+'\n')
        if not self.marker('discovery_off.observe'):return
        if self.started is None:self.started=time.monotonic_ns()
        if time.monotonic_ns()-self.started<1_000_000_000:return
        if not all(f.done() for f,wanted in self.futures.values()):return
        validate_data(self.received,self.run,self.nonce,self.role)
        if set(self.served)!={'off','on'}:return
        if any(f.result().sum!=wanted for f,wanted in self.futures.values()):raise ValueError('OFF/control RPC result differs')
        value={'run_id':self.run,'nonce':self.nonce,'role':self.role,'settings':self.settings,'snapshots':snapshots,'counts':counts,'ready':ready,'received':self.received,'served':self.served,
               'results':{k:f.result().sum for k,(f,wanted) in self.futures.items()},'sent':{m:payloads(self.run,self.nonce,self.role,m) for m in ('off','on')},'observation_ns':time.monotonic_ns()-self.started}
        write_json(self.root,'discovery_off.json',value);print('DISCOVERY_OFF_RESULT '+json.dumps(value),flush=True)
        (self.root/'hidden_cli.done').write_text(self.nonce+'\n');self.done=True
