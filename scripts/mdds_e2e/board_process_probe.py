"""Observe peer CLI-created nodes, ordered output and graph withdrawal."""
import json
import re


class ProcessProbe:
    def __init__(self,root,run,nonce,board,peer,node):
        from std_msgs.msg import String
        from rclpy.qos import QoSProfile,ReliabilityPolicy
        self.root,self.run,self.nonce,self.board,self.peer,self.node=root,run,nonce,board,peer,node
        self.space='/process_'+run;self.topic=self.space+'/'+peer+'/out';self.received=[];self.saved=False;self.gone=False
        def callback(message):
            match=re.fullmatch('Hello World: ([1-9][0-9]*)',message.data);assert match
            if self.received:assert int(match[1])==int(self.received[-1].split(': ')[1])+1
            self.received.append(message.data);assert len(self.received)<=40
        self.subscription=node.create_subscription(String,self.topic,callback,QoSProfile(depth=32,reliability=ReliabilityPolicy.RELIABLE))
    def save(self,stage,**data):
        value={'run_id':self.run,'nonce':self.nonce,'board':self.board,'peer_role':self.peer,'stage':stage,**data}
        path=self.root/('process_'+stage+'.json');tmp=path.with_suffix('.tmp')
        with tmp.open('x') as out:out.write(json.dumps(value)+'\n')
        tmp.replace(path);print('CLI_PROCESS_PROOF '+json.dumps(value),flush=True)
    def tick(self):
        if self.gone:return
        node=('run_'+self.peer,self.space);nodes=self.node.get_node_names_and_namespaces();infos=self.node.get_publishers_info_by_topic(self.topic)
        if not self.saved and len(self.received)>=3 and infos:
            assert len(infos)==1 and nodes.count(node)==1
            ep=infos[0];assert ep.node_name==node[0] and ep.node_namespace==node[1] and ep.topic_type=='std_msgs/msg/String'
            self.save('received',received=list(self.received),endpoint={'node':ep.node_name,'namespace':ep.node_namespace,'type':ep.topic_type,'type_hash':str(ep.topic_type_hash),'gid':list(ep.endpoint_gid)})
            self.saved=True
        elif self.saved and not infos and node not in nodes:
            self.save('gone',node_absent=True,publisher_absent=True);self.gone=True
