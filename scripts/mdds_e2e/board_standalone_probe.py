"""Observe the opposite CLI's standalone component and its graph withdrawal."""
import json
import re


class StandaloneProbe:
    def __init__(self,root,run,nonce,board,peer,node):
        from std_msgs.msg import String
        from rclpy.qos import QoSProfile,ReliabilityPolicy
        self.root,self.run,self.nonce,self.board,self.peer,self.node=root,run,nonce,board,peer,node
        self.space='/components_'+run;self.topic=self.space+'/'+peer+'/solo/out';self.received=[];self.saved=False;self.gone=False
        def callback(message):
            match=re.fullmatch('Hello World: ([1-9][0-9]*)',message.data);assert match
            if self.received:assert int(match[1])==int(self.received[-1].split(': ')[1])+1
            self.received.append(message.data);assert len(self.received)<=120
        self.subscription=node.create_subscription(String,self.topic,callback,QoSProfile(depth=32,reliability=ReliabilityPolicy.RELIABLE))
    def save(self,stage,**data):
        value={'run_id':self.run,'nonce':self.nonce,'board':self.board,'peer_role':self.peer,'stage':stage,**data}
        (self.root/('standalone_'+stage+'.json')).write_text(json.dumps(value)+'\n')
        print('CLI_STANDALONE_PROOF '+json.dumps(value),flush=True)
    def tick(self):
        if self.gone:return
        nodes=self.node.get_node_names_and_namespaces();infos=self.node.get_publishers_info_by_topic(self.topic)
        component=('solo_'+self.peer,self.space);container=('solo_container_'+self.peer+'_'+self.run,'/')
        if not self.saved and len(self.received)>=2 and infos:
            assert len(infos)==1 and nodes.count(component)==1 and nodes.count(container)==1
            ep=infos[0];assert ep.node_name==component[0] and ep.node_namespace==component[1] and ep.topic_type=='std_msgs/msg/String'
            self.save('received',received=list(self.received),endpoint={'node':ep.node_name,'namespace':ep.node_namespace,'type':ep.topic_type,'type_hash':str(ep.topic_type_hash),'gid':list(ep.endpoint_gid)})
            self.saved=True
        elif self.saved and not infos and component not in nodes and container not in nodes:
            self.save('gone',component_absent=True,container_absent=True,publisher_absent=True);self.gone=True
