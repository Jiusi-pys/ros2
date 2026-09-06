"""Observe real peer Talker data and selective component graph retirement."""
import json
import re


class ComponentProbe:
    def __init__(self,root,run,nonce,board,peer,node):
        from std_msgs.msg import String
        from rclpy.qos import QoSProfile,ReliabilityPolicy
        self.root,self.run,self.nonce,self.board,self.peer,self.node=root,run,nonce,board,peer,node
        self.space='/components_'+run;self.received={'primary':[],'survivor':[]};self.loaded=False;self.retired=False;self.empty=False;self.retired_baseline=None
        self.subscriptions=[]
        for name in self.received:
            def receive(message,name=name):
                match=re.fullmatch('Hello World: ([0-9]+)',message.data);assert match and int(match[1])>0
                if self.received[name]:assert int(match[1])==int(self.received[name][-1].split(': ')[1])+1
                self.received[name].append(message.data);assert len(self.received[name])<=120
            self.subscriptions.append(node.create_subscription(String,self.topic(name),receive,QoSProfile(depth=32,reliability=ReliabilityPolicy.RELIABLE)))
    def topic(self,name):return self.space+'/'+self.peer+'/'+name+'/out'
    def endpoint(self,name):
        infos=self.node.get_publishers_info_by_topic(self.topic(name))
        if not infos:return None
        assert len(infos)==1
        ep=infos[0];assert ep.node_name==name+'_'+self.peer and ep.node_namespace==self.space and ep.topic_type=='std_msgs/msg/String'
        return {'node':ep.node_name,'namespace':ep.node_namespace,'type':ep.topic_type,'type_hash':str(ep.topic_type_hash),'gid':list(ep.endpoint_gid)}
    def save(self,stage,**kwargs):
        value={'run_id':self.run,'nonce':self.nonce,'board':self.board,'peer_role':self.peer,'stage':stage,**kwargs}
        (self.root/('components_'+stage+'.json')).write_text(json.dumps(value)+'\n')
        print('CLI_COMPONENT_PROOF '+json.dumps(value),flush=True)
    def tick(self):
        if self.empty:return
        primary=self.endpoint('primary');survivor=self.endpoint('survivor')
        nodes=self.node.get_node_names_and_namespaces()
        primary_node=('primary_'+self.peer,self.space);survivor_node=('survivor_'+self.peer,self.space)
        if not self.loaded and primary and survivor and all(len(v)>=2 for v in self.received.values()):
            assert nodes.count(primary_node)==1 and nodes.count(survivor_node)==1
            self.save('loaded',primary=primary,survivor=survivor,received={k:list(v) for k,v in self.received.items()});self.loaded=True
        elif self.loaded and not self.retired and primary is None and primary_node not in nodes and survivor:
            if self.retired_baseline is None:self.retired_baseline=len(self.received['survivor'])
            if len(self.received['survivor'])>self.retired_baseline:
                self.save('retired',primary_absent=True,survivor=survivor,survivor_after=self.received['survivor'][-1]);self.retired=True
        elif self.retired and primary is None and survivor is None and primary_node not in nodes and survivor_node not in nodes:
            assert ('container_'+self.peer,self.space) in nodes
            self.save('empty',primary_absent=True,survivor_absent=True,container_visible=True);self.empty=True
