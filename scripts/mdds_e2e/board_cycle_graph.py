"""Observe local graph survival and exact restoration while ROS contexts remain alive."""
import json
from abrupt_graph_contract import write_json
from cycle_graph_contract import collect,local_view,validate_views

class CycleGraphProbe:
    def __init__(self,root,run,nonce,role,records,graph_ok):
        from std_msgs.msg import String
        from example_interfaces.srv import AddTwoInts
        self.root,self.run,self.nonce,self.role,self.records,self.graph_ok=root,run,nonce,role,records,graph_ok
        self.node=records[0]['node'];self.ns='/ros_broker_'+run;self.String,self.Service=String,AddTwoInts
        self.before=None;self.paused=None;self.restored=None;self.sent=False;self.received=[];self.future=None;self.closed=False;self.finished=False;self.pending=None
        if (root/'cycle_graph.enabled').read_text().strip()!=nonce:raise ValueError('cycle graph identity differs')
        self.topic=self.ns+'/'+role+'/cycle_local'
        self.pub=self.node.create_publisher(String,self.topic,10)
        def receive(message):
            expected=self.payloads()
            if len(self.received)>=3 or message.data!=expected[len(self.received)]:raise ValueError('local outage payload differs or duplicated')
            self.received.append(message.data);print('CYCLE_LOCAL_RX '+json.dumps({'run_id':run,'nonce':nonce,'role':role,'data':message.data}),flush=True)
        self.sub=records[1]['node'].create_subscription(String,self.topic,receive,10)
    def payloads(self):return [f'{self.run}|{self.nonce}|{self.role}|local|{i}' for i in range(3)]
    def emit(self,stage,value):
        record={'run_id':self.run,'nonce':self.nonce,'role':self.role,**value}
        write_json(self.root,'reconnect.graph_'+stage+'.json',record);print('CYCLE_GRAPH_'+stage.upper()+' '+json.dumps(record),flush=True)
    def close(self):
        if not self.closed:
            self.node.destroy_publisher(self.pub);self.records[1]['node'].destroy_subscription(self.sub);self.closed=True
    def tick(self):
        if self.finished or not (self.root/'phase1.done').exists():return
        from rclpy.node import NodeNameNonExistentError
        try:
            if self.before is None:
                if not self.graph_ok():return
                self.before=collect(self.node,self.ns);self.emit('ready',{'snapshot':self.before});return
            if self.paused is None:
                if not (self.root/'reconnect.paused.json').exists():return
                snapshot=collect(self.node,self.ns,self.before)
                if snapshot!=local_view(self.before,self.role):return
                r=self.records[0]
                if r['client'].service_is_ready() or r['pub'].get_subscription_count()!=0:return
                if not self.sent:
                    if self.pub.get_subscription_count()!=1:return
                    for text in self.payloads():message=self.String();message.data=text;self.pub.publish(message)
                    self.sent=True
                if self.received!=self.payloads():return
                self.paused=snapshot;self.emit('paused',{'snapshot':snapshot,'local_received':list(self.received),'remote_service_ready':False,'remote_subscriptions':0});return
            if self.restored is None:
                if not (self.root/'reconnect.restored.json').exists() or not self.graph_ok():return
                snapshot=collect(self.node,self.ns,self.before);validate_views(self.before,self.paused,snapshot,self.role)
                if self.future is None:
                    request=self.Service.Request();request.a=int(self.nonce[:7],16)+(1 if self.role=='A' else 2);request.b=177717
                    self.future=self.records[0]['client'].call_async(request);return
                if not self.future.done():return
                operand=int(self.nonce[:7],16)+(1 if self.role=='A' else 2)
                if self.future.result().sum!=operand+177717:raise RuntimeError('restored RPC response differs')
                self.restored=snapshot;self.emit('restored',{'snapshot':snapshot,'a':operand,'b':177717,'sum':self.future.result().sum});return
        except (ValueError,NodeNameNonExistentError) as error:
            if str(error)!=self.pending:self.pending=str(error);print('CYCLE_GRAPH_PENDING '+self.pending,flush=True)
            return
        p=self.root/'cycle_graph.release'
        if not p.exists():return
        if p.read_text().strip()!=self.nonce:raise ValueError('cycle graph release differs')
        self.close()
        topics={n for n,t in self.node.get_topic_names_and_types()}
        for role in ('A','B'):
            name=self.ns+'/'+role+'/cycle_local'
            if name in topics or self.node.count_publishers(name) or self.node.count_subscribers(name):return
        self.emit('done',{'local_probe_removed':True});self.finished=True
