"""Keep graph API visibility complete while exercising CLI hidden filtering."""
import json
from hidden_graph_contract import NODES,scope,groups,validate_snapshot


class HiddenGraphSource:
    def __init__(self,root,run,nonce,role,node,executor):
        self.root,self.run,self.nonce,self.role,self.node,self.executor=root,run,nonce,role,node,executor
        self.nodes=[];self.actions=[];self.ready=False;self.stopped=False
    def tick(self):
        if self.stopped:return
        if not self.nodes:
            if not (self.root/'hidden_source.go').exists():return
            if (self.root/'hidden_source.go').read_text().strip()!=self.nonce:raise ValueError('hidden source barrier differs')
            self.create()
        if not self.ready:
            prefix=scope(self.run)+'/'
            try:
                data={'nodes':{s.rstrip('/')+'/'+n:[] for n,s in self.node.get_node_names_and_namespaces() if s.startswith(scope(self.run)+'/')},
                      'topics':{n:t for n,t in self.node.get_topic_names_and_types() if n.startswith(prefix)},
                      'native_topics':{n:t for n,t in self.node.get_topic_names_and_types(no_demangle=True) if n[2:].startswith(prefix)},'native_by_node':{}}
                for role in ('A','B'):
                    ns=scope(self.run)+'/'+role
                    for name in NODES:
                        data['native_by_node'][ns+'/'+name]={'publishers':dict(self.node.get_publisher_names_and_types_by_node(name,ns,no_demangle=True)),
                                                           'subscriptions':dict(self.node.get_subscriber_names_and_types_by_node(name,ns,no_demangle=True))}
                validate_snapshot(data,self.run)
            except (RuntimeError,ValueError) as error:
                (self.root/'hidden_source.pending').write_text(str(error));return
            value={'run_id':self.run,'nonce':self.nonce,'role':self.role,'snapshot':data}
            (self.root/'hidden_source.json').write_text(json.dumps(value)+'\n');self.ready=True
            print('HIDDEN_GRAPH_SOURCE '+json.dumps(value),flush=True)
        if (self.root/'hidden_source.stop').exists():
            if (self.root/'hidden_source.stop').read_text().strip()!=self.nonce:raise ValueError('hidden stop differs')
            for action in self.actions:action.destroy()
            for node in self.nodes:self.executor.remove_node(node);node.destroy_node()
            self.nodes=[];self.actions=[];self.stopped=True
            (self.root/'hidden_source.done').write_text(self.nonce+'\n')
    def create(self):
        from rclpy.node import Node
        from rclpy.action import ActionClient,ActionServer
        from std_msgs.msg import String
        from example_interfaces.srv import AddTwoInts
        from example_interfaces.action import Fibonacci
        peer='B' if self.role=='A' else 'A'
        for name in NODES:
            node=Node(name,namespace=scope(self.run)+'/'+self.role,context=self.node.context,start_parameter_services=False,enable_rosout=False)
            self.nodes.append(node);self.executor.add_node(node)
            for group in groups(name):
                base=scope(self.run)+'/'+self.role+'/'+group;target=scope(self.run)+'/'+peer+'/'+group
                node.create_publisher(String,base+'/out',10);node.create_subscription(String,target+'/out',lambda message:None,10)
                def serve(request,response):response.sum=request.a+request.b;return response
                node.create_service(AddTwoInts,base+'/serve',serve);node.create_client(AddTwoInts,target+'/serve')
                def action(handle):result=Fibonacci.Result();result.sequence=[0,1];handle.succeed();return result
                self.actions.append(ActionServer(node,Fibonacci,base+'/action',action));self.actions.append(ActionClient(node,Fibonacci,target+'/action'))
