"""Run real cross-board pub/sub with compatible and incompatible QoS."""
import json
import time
from endpoint_qos_contract import MATRIX,qos,topic,payloads,validate,validate_counts


class EndpointQoSProbe:
    def __init__(self,root,run,nonce,role,node):
        from std_msgs.msg import String
        from rclpy.duration import Duration
        from rclpy.qos import QoSProfile,ReliabilityPolicy,DurabilityPolicy,HistoryPolicy,LivelinessPolicy,qos_check_compatible
        self.root,self.run,self.nonce,self.role,self.node=root,run,nonce,role,node
        self.peer='B' if role=='A' else 'A';self.String=String;self.pubs={};self.subs={};self.sent={};self.received={}
        self.ready=False;self.done=False;self.next_send=0;self.sent_end=None;self.observe_start=None
        self.last_pending=None
        self.compatibility={}
        for item in MATRIX:
            name=item['name'];self.sent[name]=[];self.received[name]=[]
            def profile(direction):
                q=qos(name,direction)
                return QoSProfile(history=HistoryPolicy(q['history']),depth=q['depth'],reliability=ReliabilityPolicy(q['reliability']),durability=DurabilityPolicy(q['durability']),
                    deadline=Duration(nanoseconds=q['deadline']),lifespan=Duration(nanoseconds=q['lifespan']),liveliness=LivelinessPolicy(q['liveliness']),liveliness_lease_duration=Duration(nanoseconds=q['lease']))
            offered,requested=profile('offered'),profile('requested')
            compatibility,reason=qos_check_compatible(offered,requested)
            self.compatibility[name]={'code':int(compatibility),'reason':reason}
            self.pubs[name]=node.create_publisher(String,topic(run,role,name),offered)
            def receive(message,name=name,compatible=item['compatible']):
                if not compatible:raise RuntimeError('incompatible QoS delivered a sample: '+name)
                expected=payloads(run,nonce,self.peer,name);values=self.received[name]
                if len(values)>=len(expected) or message.data!=expected[len(values)]:raise ValueError('QoS sample identity/order differs')
                values.append(message.data)
                print('ENDPOINT_QOS_RX '+json.dumps({'role':role,'case':name,'payload':message.data}),flush=True)
            self.subs[name]=node.create_subscription(String,topic(run,self.peer,name),receive,requested)
    def metadata(self):
        result={}
        for item in MATRIX:
            name=item['name'];target=topic(self.run,self.peer,name);result[name]={}
            for kind,getter in (('publisher',self.node.get_publishers_info_by_topic),('subscription',self.node.get_subscriptions_info_by_topic)):
                infos=getter(target)
                if len(infos)!=1:raise ValueError('endpoint metadata not yet complete')
                info=infos[0];q=info.qos_profile
                result[name][kind]={'node':info.node_name,'namespace':info.node_namespace,'type':info.topic_type,'type_hash':str(info.topic_type_hash),
                    'direction':int(info.endpoint_type),'gid':list(info.endpoint_gid),
                    'qos':{'history':int(q.history),'depth':q.depth,'reliability':int(q.reliability),'durability':int(q.durability),
                           'deadline':q.deadline.nanoseconds,'lifespan':q.lifespan.nanoseconds,'liveliness':int(q.liveliness),'lease':q.liveliness_lease_duration.nanoseconds}}
        return result
    def tick(self):
        if self.done:return
        counts={name:{'publishers':self.node.count_publishers(topic(self.run,self.peer,name)),'subscriptions':self.node.count_subscribers(topic(self.run,self.peer,name)),
                      'writer_matches':self.pubs[name].get_subscription_count(),'reader_matches':self.subs[name].get_publisher_count()} for name in self.pubs}
        try:
            for name,values in counts.items():validate_counts(name,values)
            metadata=self.metadata()
        except ValueError as error:
            (self.root/'endpoint_qos.pending').write_text(str(error))
            pending=json.dumps({'error':str(error),'counts':counts})
            (self.root/'endpoint_qos.pending.json').write_text(pending+'\n')
            if pending!=self.last_pending:
                print('ENDPOINT_QOS_PENDING '+pending,flush=True)
                with (self.root/'endpoint_qos.diagnostics.jsonl').open('a') as stream:stream.write(pending+'\n')
                self.last_pending=pending
            return
        if not self.ready:
            (self.root/'endpoint_qos.ready').write_text(self.nonce+'\n');self.ready=True
        if not (self.root/'endpoint_qos.go').exists():return
        if (self.root/'endpoint_qos.go').read_text().strip()!=self.nonce:raise ValueError('QoS send barrier differs')
        now=time.monotonic_ns()
        if self.sent_end is None and now>=self.next_send:
            for name,pub in self.pubs.items():
                message=self.String();message.data=payloads(self.run,self.nonce,self.role,name)[len(self.sent[name])]
                pub.publish(message);self.sent[name].append(message.data)
            self.next_send=now+100_000_000
            if all(len(v)==3 for v in self.sent.values()):
                self.sent_end=now;(self.root/'endpoint_qos.sent').write_text(self.nonce+'\n')
        if self.sent_end is None or not (self.root/'endpoint_qos.observe_go').exists():return
        if (self.root/'endpoint_qos.observe_go').read_text().strip()!=self.nonce:raise ValueError('QoS observation barrier differs')
        if self.observe_start is None:self.observe_start=now
        if now-self.observe_start<1_000_000_000:return
        if any(len(self.received[v['name']])!=3 for v in MATRIX if v['compatible']):return
        value={'run_id':self.run,'nonce':self.nonce,'role':self.role,'passed':True,'observation_ns':now-self.observe_start,
               'counts':counts,'metadata':metadata,'sent':self.sent,'received':self.received,'compatibility':self.compatibility}
        type_hash=json.loads((self.root/'type_hashes.json').read_bytes())['hashes']['std_msgs/msg/String']
        validate(value,self.run,self.nonce,self.role,type_hash)
        (self.root/'endpoint_qos.json').write_text(json.dumps(value)+'\n');self.done=True
        print('ENDPOINT_QOS_RESULT '+json.dumps(value),flush=True)
