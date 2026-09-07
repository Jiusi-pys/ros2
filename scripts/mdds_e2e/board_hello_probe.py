"""Observe nonce-bound peer ROS hello separately from multicast diagnostics."""
import json


class HelloProbe:
    def __init__(self,root,run,nonce,board,peer,node):
        from std_msgs.msg import String
        from rclpy.qos import QoSProfile,ReliabilityPolicy
        self.root,self.run,self.nonce,self.board,self.peer,self.node=root,run,nonce,board,peer,node
        self.data={k:[] for k in ('doctor','wtf')};self.saved=set();self.gone=set();self.subs=[];self.nodes={}
        for command in ('doctor','wtf'):
            def callback(message,command=command):
                own=('A' if peer=='B' else 'B')+'_'+nonce;other=peer+'_'+nonce
                assert message.data in ("hello, it's me "+own,"hello, it's me "+other)
                if message.data.endswith(other):self.data[command].append(message.data)
                assert len(self.data[command])<=300
            self.subs.append(node.create_subscription(String,'/hello_'+run+'/'+command,callback,QoSProfile(depth=32,reliability=ReliabilityPolicy.RELIABLE)))
    def save(self,command,stage,**data):
        value={'run_id':self.run,'nonce':self.nonce,'board':self.board,'peer_role':self.peer,'command':command,'stage':stage,**data}
        path=self.root/('hello_'+command+'_'+stage+'.json');temporary=path.with_suffix('.tmp')
        with temporary.open('x') as f:f.write(json.dumps(value)+'\n')
        temporary.replace(path);print('CLI_HELLO_PROOF '+json.dumps(value),flush=True)
    def tick(self):
        for command in ('doctor','wtf'):
            infos=self.node.get_publishers_info_by_topic('/hello_'+self.run+'/'+command)
            if command not in self.saved and len(self.data[command])>=3 and len(infos)==2:
                self.nodes[command]=[(e.node_name,e.node_namespace) for e in infos]
                self.save(command,'received',received=list(self.data[command]),publishers=[{'node':e.node_name,'namespace':e.node_namespace,'gid':list(e.endpoint_gid),'type':e.topic_type,'type_hash':str(e.topic_type_hash)} for e in infos]);self.saved.add(command)
            elif command in self.saved and command not in self.gone and not infos and all(node not in self.node.get_node_names_and_namespaces() for node in self.nodes[command]):
                self.save(command,'gone',publishers_absent=True,nodes_absent=True);self.gone.add(command)
