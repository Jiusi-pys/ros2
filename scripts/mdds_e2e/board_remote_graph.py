"""Apply graph mutations only after the opposite board arms its waiters."""
import json
from remote_graph_contract import PHASES,change_record


class RemoteGraphSource:
    def __init__(self,root,run,nonce,role,node):
        self.root,self.run,self.nonce,self.role,self.node=root,run,nonce,role,node
        self.index=0;self.entity=None;self.space='/remote_guard_'+run+'/'+role
    def tick(self):
        if self.index==len(PHASES):return
        marker=self.root/('remote_change_'+str(self.index)+'.go')
        if not marker.exists():return
        if marker.read_text().strip()!=self.nonce:raise ValueError('remote graph mutation nonce differs')
        from std_msgs.msg import String
        from example_interfaces.srv import AddTwoInts
        from rclpy.impl.implementation_singleton import rclpy_implementation as _rclpy
        kind,operation=PHASES[self.index].split('_');topic=self.space+'/topic';service=self.space+'/service'
        if operation=='create':
            if self.entity is not None:raise ValueError('remote source still owns preceding entity')
            if kind=='publisher':self.entity=self.node.create_publisher(String,topic,10)
            elif kind=='subscription':self.entity=self.node.create_subscription(String,topic,lambda msg:None,10)
            elif kind=='service':
                def callback(request,response):response.sum=request.a+request.b;return response
                self.entity=self.node.create_service(AddTwoInts,service,callback)
            elif kind=='client':self.entity=self.node.create_client(AddTwoInts,service)
            else:
                with self.node.context.handle:
                    self.entity=_rclpy.Node('added',self.space,self.node.context.handle,[],False,False)
                for getter in (self.node.get_publisher_names_and_types_by_node,self.node.get_subscriber_names_and_types_by_node,
                               self.node.get_service_names_and_types_by_node,self.node.get_client_names_and_types_by_node):
                    if getter('added',self.space):raise ValueError('bare remote node unexpectedly owns endpoints')
        else:
            if self.entity is None:raise ValueError('remote source entity missing')
            if kind=='node':self.entity.destroy_when_not_in_use()
            elif not getattr(self.node,'destroy_'+kind)(self.entity):raise ValueError('remote source entity destruction failed')
            self.entity=None
        value=change_record(self.run,self.nonce,self.role,self.index)
        (self.root/('remote_source_'+str(self.index)+'.json')).write_text(json.dumps(value)+'\n')
        print('GRAPH_REMOTE_SOURCE '+json.dumps(value),flush=True)
        tmp=self.root/('remote_source_'+str(self.index)+'.pending');tmp.write_text(self.nonce+'\n')
        tmp.replace(self.root/('remote_source_'+str(self.index)+'.done'));self.index+=1
