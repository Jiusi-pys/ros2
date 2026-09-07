"""Publish finite peer bag fixtures and observe exact recorded/replayed samples."""
import json
import time
from bag_contract import FORMATS,topic,payloads


class BagProbe:
    def __init__(self,root,run,nonce,board,peer_board,role,node):
        from std_msgs.msg import String
        from rclpy.qos import QoSProfile,ReliabilityPolicy,DurabilityPolicy
        self.root,self.run,self.nonce,self.board,self.peer_board,self.role,self.node=root,run,nonce,board,peer_board,role,node
        self.peer='B' if role=='A' else 'A';self.publishers={};self.subs=[];self.sent={s:0 for s in FORMATS};self.next_send={s:0 for s in FORMATS}
        self.received={s:{k:[] for k in ('main','noise')} for s in FORMATS};self.playback={s:[] for s in FORMATS};self.String=String
        self.last_matches={}
        self.bursts={s:[] for s in FORMATS}
        qos=QoSProfile(depth=32,reliability=ReliabilityPolicy.RELIABLE)
        for storage in FORMATS:
            for kind in ('main','noise'):
                self.publishers[(storage,kind)]=node.create_publisher(String,topic(run,role,storage,kind),qos)
                expected=payloads(run,nonce,peer_board,storage,kind)
                def receive(message,storage=storage,kind=kind,expected=expected):
                    values=self.received[storage][kind];assert len(values)<5 and message.data==expected[len(values)];values.append(message.data)
                    if all(len(v)==5 for v in self.received[storage].values()):self.save('received',storage,received=self.received[storage])
                self.subs.append(node.create_subscription(String,topic(run,self.peer,storage,kind),receive,qos))
            expected=payloads(run,nonce,board,storage,'main')
            def replay(message,storage=storage,expected=expected):
                values=self.playback[storage];assert len(values)<5 and message.data==expected[len(values)];values.append(message.data)
                if len(values)==5:self.save('played',storage,received=values)
            self.subs.append(node.create_subscription(String,topic(run,self.peer,storage,'play'),replay,qos))
            if (root/'cli_batch').read_text().strip()=='bag_burst':
                burst_qos=QoSProfile(depth=32,reliability=ReliabilityPolicy.RELIABLE,durability=DurabilityPolicy.TRANSIENT_LOCAL)
                expected=payloads(run,nonce,board,storage,'main')[:3]
                def receive_burst(message,storage=storage,expected=expected):
                    values=self.bursts[storage];assert len(values)<3 and message.data==expected[len(values)];values.append(message.data)
                    if len(values)==3:self.save('burst',storage,received=values)
                self.subs.append(node.create_subscription(String,topic(run,self.peer,storage,'burst'),receive_burst,burst_qos))
    def save(self,stage,storage,**data):
        value={'run_id':self.run,'nonce':self.nonce,'board':self.board,'storage':storage,'stage':stage,**data}
        (self.root/('bag_'+storage+'_'+stage+'.json')).write_text(json.dumps(value)+'\n')
        print('CLI_BAG_PROOF '+json.dumps(value),flush=True)
    def tick(self):
        for storage in FORMATS:
            if self.sent[storage]>=5 or not (self.root/('bag_'+storage+'.go')).exists():continue
            assert (self.root/('bag_'+storage+'.go')).read_text().strip()==self.nonce
            counts={kind:self.publishers[(storage,kind)].get_subscription_count() for kind in ('main','noise')}
            if self.last_matches.get(storage)!=counts:
                endpoints={kind:[{'node':e.node_name,'namespace':e.node_namespace,'type':e.topic_type,'qos':str(e.qos_profile),'gid':list(e.endpoint_gid)} for e in self.node.get_subscriptions_info_by_topic(topic(self.run,self.role,storage,kind))] for kind in ('main','noise')}
                print('CLI_BAG_MATCHES '+json.dumps({'storage':storage,'counts':counts,'endpoints':endpoints}),flush=True)
                self.last_matches[storage]=counts
            if any(count!=2 for count in counts.values()):continue
            now=time.monotonic()
            if now<self.next_send[storage]:continue
            index=self.sent[storage]
            for kind in ('main','noise'):
                message=self.String();message.data=payloads(self.run,self.nonce,self.board,storage,kind)[index]
                self.publishers[(storage,kind)].publish(message)
            self.sent[storage]+=1;self.next_send[storage]=now+.1
            if self.sent[storage]==5:self.save('sent',storage,sent={kind:payloads(self.run,self.nonce,self.board,storage,kind) for kind in ('main','noise')})
