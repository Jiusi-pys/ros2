"""Construct the actual lifecycle node and retain its callback evidence."""
import json


def create(node_name,root,run,nonce,board,**kwargs):
    from rclpy.lifecycle import LifecycleNode,TransitionCallbackReturn
    callbacks=[]
    class Fixture(LifecycleNode):
        def record(self,label,state):
            callbacks.append({'callback':label,'previous':{'id':state.state_id,'label':state.label}})
            value={'run_id':run,'nonce':nonce,'board':board,'node':self.get_fully_qualified_name(),'callbacks':callbacks}
            (root/'lifecycle_callbacks.json').write_text(json.dumps(value)+'\n')
            print('CLI_LIFECYCLE_CALLBACK '+json.dumps(callbacks[-1]),flush=True)
            return TransitionCallbackReturn.SUCCESS
        def on_configure(self,state):return self.record('configure',state)
        def on_activate(self,state):return self.record('activate',state)
        def on_deactivate(self,state):return self.record('deactivate',state)
        def on_cleanup(self,state):return self.record('cleanup',state)
        def on_shutdown(self,state):return self.record('shutdown',state)
    return Fixture(node_name,**kwargs)
