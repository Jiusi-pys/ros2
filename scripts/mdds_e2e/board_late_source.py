"""Establish two graph-rich nodes before permitting a new observer to start."""
import json
import os
import time
from late_graph_contract import NODES,space
from late_graph_snapshot import collect
from board_graph_ownership import process_start


class LateGraphSource:
    def __init__(self,root,run,nonce,role,node,executor):
        self.root,self.run,self.nonce,self.role,self.node,self.executor=root,run,nonce,role,node,executor
        self.nodes=[];self.actions=[];self.pubs=[];self.ready=False;self.stopped=False
        self.last_error=None
        self.hashes=json.loads((root/'late_graph_hashes.json').read_bytes())['hashes']
    def tick(self):
        if self.stopped:return
        if not self.nodes:
            if not (self.root/'late_source.go').exists():return
            if (self.root/'late_source.go').read_text().strip()!=self.nonce:raise ValueError('late source start differs')
            self.create()
        if not self.ready:
            try:
                snapshot=collect(self.node,self.run,self.role,self.hashes)
                collect(self.node,self.run,'B' if self.role=='A' else 'A',self.hashes)
            except (RuntimeError,ValueError) as error:
                if str(error)!=self.last_error:
                    self.last_error=str(error);print('LATE_SOURCE_PENDING '+self.last_error,flush=True)
                    (self.root/'late_source.error').write_text(self.last_error)
                return
            value={'run_id':self.run,'nonce':self.nonce,'role':self.role,'pid':os.getpid(),'start':process_start(os.getpid()),
                   'ready_ns':time.monotonic_ns(),'context_shared':all(n.context is self.node.context for n in self.nodes),'snapshot':snapshot}
            (self.root/'late_source.json').write_text(json.dumps(value)+'\n');self.ready=True
            print('LATE_SOURCE_READY '+json.dumps(value),flush=True)
        if (self.root/'late_source.stop').exists():
            if (self.root/'late_source.stop').read_text().strip()!=self.nonce:raise ValueError('late source stop differs')
            for action in self.actions:action.destroy()
            for node in self.nodes:self.executor.remove_node(node);node.destroy_node()
            self.nodes=[];self.actions=[];self.pubs=[];self.stopped=True
            (self.root/'late_source.done').write_text(self.nonce+'\n')
    def create(self):
        from rclpy.node import Node
        from rclpy.action import ActionClient,ActionServer
        from rclpy.qos import QoSProfile,ReliabilityPolicy,DurabilityPolicy,HistoryPolicy
        from std_msgs.msg import String
        from example_interfaces.srv import AddTwoInts
        from example_interfaces.action import Fibonacci
        peer='B' if self.role=='A' else 'A'
        for name in NODES:
            node=Node(name,namespace=space(self.run,self.role),context=self.node.context,start_parameter_services=False,enable_rosout=False)
            self.nodes.append(node);self.executor.add_node(node)
            qos=QoSProfile(depth=7 if name=='one' else 11,reliability=ReliabilityPolicy.RELIABLE,durability=DurabilityPolicy.VOLATILE,history=HistoryPolicy.KEEP_LAST)
            base=space(self.run,self.role)+'/'+name;target=space(self.run,peer)+'/'+name
            self.pubs.append(node.create_publisher(String,base+'/out',qos))
            node.create_subscription(String,target+'/out',lambda message:None,qos)
            def serve(request,response):response.sum=request.a+request.b;return response
            node.create_service(AddTwoInts,base+'/serve',serve,qos_profile=qos);node.create_client(AddTwoInts,target+'/serve',qos_profile=qos)
            def action(handle):result=Fibonacci.Result();result.sequence=[0,1];handle.succeed();return result
            self.actions.append(ActionServer(node,Fibonacci,base+'/action',action,goal_service_qos_profile=qos,result_service_qos_profile=qos,cancel_service_qos_profile=qos,feedback_pub_qos_profile=qos,status_pub_qos_profile=qos))
            other_action=space(self.run,peer)+'/'+('two' if name=='one' else 'one')+'/action'
            self.actions.append(ActionClient(node,Fibonacci,other_action,goal_service_qos_profile=qos,result_service_qos_profile=qos,cancel_service_qos_profile=qos,feedback_sub_qos_profile=qos,status_sub_qos_profile=qos))
