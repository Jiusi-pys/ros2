"""Observe peer CLI-created nodes, ordered output and graph withdrawal."""
import json
import re


class ProcessProbe:
    def __init__(self,root,run,nonce,board,peer,node,language='cpp'):
        from std_msgs.msg import String
        from rclpy.qos import QoSProfile,ReliabilityPolicy
        self.root,self.run,self.nonce,self.board,self.peer,self.node=root,run,nonce,board,peer,node
        self.space='/process_'+run;self.topic=self.space+'/'+peer+'/out';self.received=[];self.saved=False;self.gone=False
        self.kind=(root/'cli_batch').read_text().strip().removeprefix('process_')
        assert self.kind in ('run','launch','test')
        self.prefix='process';self.minimum=1
        if language=='python':self.kind='run_python';self.topic=self.space+'/'+peer+'/python_out';self.prefix='python_process';self.minimum=0
        def callback(message):
            match=re.fullmatch('Hello World: (0|[1-9][0-9]*)',message.data);assert match and int(match[1])>=self.minimum
            if self.received:assert int(match[1])==int(self.received[-1].split(': ')[1])+1
            self.received.append(message.data);assert len(self.received)<=40
        self.subscription=node.create_subscription(String,self.topic,callback,QoSProfile(depth=32,reliability=ReliabilityPolicy.RELIABLE))
    def save(self,stage,**data):
        value={'run_id':self.run,'nonce':self.nonce,'board':self.board,'peer_role':self.peer,'kind':self.kind,'stage':stage,**data}
        path=self.root/(self.prefix+'_'+stage+'.json');tmp=path.with_suffix('.tmp')
        with tmp.open('x') as out:out.write(json.dumps(value)+'\n')
        tmp.replace(path);print('CLI_PROCESS_PROOF '+json.dumps(value),flush=True)
    def tick(self):
        if self.gone:return
        node=(self.kind+'_'+self.peer,self.space);nodes=self.node.get_node_names_and_namespaces();infos=self.node.get_publishers_info_by_topic(self.topic)
        if not self.saved and len(self.received)>=3 and infos:
            assert len(infos)==1 and nodes.count(node)==1
            ep=infos[0];assert ep.node_name==node[0] and ep.node_namespace==node[1] and ep.topic_type=='std_msgs/msg/String'
            self.save('received',received=list(self.received),endpoint={'node':ep.node_name,'namespace':ep.node_namespace,'type':ep.topic_type,'type_hash':str(ep.topic_type_hash),'gid':list(ep.endpoint_gid)})
            self.saved=True
        elif self.saved and not infos and node not in nodes:
            self.save('gone',node_absent=True,publisher_absent=True);self.gone=True


class RunProbes:
    def __init__(self,*args):self.probes=[ProcessProbe(*args,language=language) for language in ('cpp','python')]
    def tick(self):
        for probe in self.probes:probe.tick()
