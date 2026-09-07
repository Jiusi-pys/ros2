"""Finite cross-board streams and a receiver-clock request for delay testing."""
import json
import time
from topic_statistics import VERBS,COUNT,topic,payload


class StatisticsProbe:
    def __init__(self,root,run,nonce,board,peer_board,role,node):
        from std_msgs.msg import String
        from geometry_msgs.msg import PointStamped
        from rclpy.qos import QoSProfile,ReliabilityPolicy
        from rclpy.serialization import serialize_message
        self.root,self.run,self.nonce,self.board,self.peer_board,self.role,self.node=root,run,nonce,board,peer_board,role,node
        self.peer='B' if role=='A' else 'A';self.String=String;self.PointStamped=PointStamped;self.serialize=serialize_message
        self.requested={};self.requests={};self.next_control={};self.next_send={};self.publishers={};self.subs=[]
        self.sent={v:[] for v in VERBS};self.received={v:[] for v in VERBS};self.receive_times={v:[] for v in VERBS};self.receive_sizes={v:[] for v in VERBS};self.send_times={v:[] for v in VERBS};self.send_monotonic={v:[] for v in VERBS}
        qos=QoSProfile(depth=32,reliability=ReliabilityPolicy.RELIABLE)
        self.control=node.create_publisher(String,topic(run,self.peer,'control'),qos)
        def request(message):
            value=json.loads(message.data);verb=value['verb']
            assert verb in VERBS and value['run_id']==run and value['nonce']==nonce and value['source']==board and value['receiver']==peer_board
            assert type(value['stamp_ns']) is int and value['stamp_ns']>0
            if verb in self.requests:assert self.requests[verb]==value
            else:self.requests[verb]=value
        self.subs.append(node.create_subscription(String,topic(run,role,'control'),request,qos))
        for verb in VERBS:
            msg_type=PointStamped if verb=='delay' else String
            self.publishers[verb]=node.create_publisher(msg_type,topic(run,role,verb),qos)
            def receive(message,verb=verb):
                values=self.received[verb];index=len(values);assert index<COUNT
                now=time.time_ns();expected=payload(run,nonce,peer_board,verb,index)
                actual=message.header.frame_id if verb=='delay' else message.data
                assert actual==expected and verb in self.requested
                if verb=='delay':
                    assert message.header.stamp.sec*1000000000+message.header.stamp.nanosec==self.requested[verb]['stamp_ns']
                    assert message.point.x==float(index) and message.point.y==20.0 and message.point.z==2.0
                values.append(actual);self.receive_times[verb].append(now);self.receive_sizes[verb].append(len(serialize_message(message)))
                if len(values)==COUNT:self.save('received',verb,data=values,receive_ns=self.receive_times[verb],sizes=self.receive_sizes[verb],stamp_ns=self.requested[verb]['stamp_ns'],request=self.requested[verb])
            self.subs.append(node.create_subscription(msg_type,topic(run,self.peer,verb),receive,qos))
    def save(self,stage,verb,**data):
        value={'run_id':self.run,'nonce':self.nonce,'board':self.board,'verb':verb,'stage':stage,**data}
        path=self.root/('stats_'+verb+'_'+stage+'.json');temporary=path.with_suffix('.tmp')
        assert not path.exists()
        with temporary.open('x') as out:out.write(json.dumps(value)+'\n')
        temporary.replace(path);print('CLI_STATS_PROOF '+json.dumps(value),flush=True)
    def tick(self):
        now=time.monotonic()
        for verb in VERBS:
            go=self.root/('stats_'+verb+'.go')
            if go.exists() and len(self.received[verb])<COUNT:
                assert go.read_text().strip()==self.nonce
                if verb not in self.requested:
                    self.requested[verb]={'run_id':self.run,'nonce':self.nonce,'verb':verb,'source':self.peer_board,'receiver':self.board,'stamp_ns':time.time_ns()-2000000000}
                if now>=self.next_control.get(verb,0):
                    message=self.String();message.data=json.dumps(self.requested[verb]);self.control.publish(message);self.next_control[verb]=now+.2
            if verb not in self.requests or len(self.sent[verb])>=COUNT or now<self.next_send.get(verb,0):continue
            if self.publishers[verb].get_subscription_count()!=2:continue
            index=len(self.sent[verb]);text=payload(self.run,self.nonce,self.board,verb,index)
            if verb=='delay':
                message=self.PointStamped();message.header.frame_id=text;message.header.stamp.sec,message.header.stamp.nanosec=divmod(self.requests[verb]['stamp_ns'],1000000000)
                message.point.x=float(index);message.point.y=20.0;message.point.z=2.0
            else:message=self.String();message.data=text
            self.send_times[verb].append(time.time_ns());self.send_monotonic[verb].append(time.monotonic_ns());self.publishers[verb].publish(message);self.sent[verb].append(text);self.next_send[verb]=now+.2
            if len(self.sent[verb])==COUNT:self.save('sent',verb,data=self.sent[verb],publish_ns=self.send_times[verb],monotonic_ns=self.send_monotonic[verb],request=self.requests[verb])
