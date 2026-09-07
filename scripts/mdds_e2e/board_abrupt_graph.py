"""Observe bounded victim removal and prove survivors continue peer traffic."""
import json
import time
from abrupt_graph_contract import scope,BOUND_NS,write_json,validate_pair,validate_kill
from abrupt_graph_snapshot import collect,empty
from abrupt_entities import Entities

class AbruptGraphSource:
    def __init__(self,root,run,nonce,role,observer,executor):
        self.root,self.run,self.nonce,self.role,self.observer,self.executor=root,run,nonce,role,observer,executor
        self.entities=None;self.before=None;self.armed=None;self.removed=None;self.after=None;self.closed=False;self.finished=False;self.pending=None
    def marker(self,name):
        p=self.root/name
        if not p.exists():return False
        if p.read_text().strip()!=self.nonce:raise ValueError('abrupt marker differs: '+name)
        return True
    def close(self):
        if self.entities is not None and not self.closed:
            self.executor.remove_node(self.entities.node);self.entities.close();self.closed=True
    def tick(self):
        if self.finished:return
        if self.entities is None:
            if not self.marker('hidden_source.go'):return
            from rclpy.node import Node
            node=Node('survivor_'+self.role,namespace=scope(self.run),context=self.observer.context,start_parameter_services=False,enable_rosout=False)
            self.executor.add_node(node);self.entities=Entities(node,self.run,self.nonce,self.role,'survivor')
        from rclpy.node import NodeNameNonExistentError
        if self.before is None:
            try:snapshot=collect(self.observer,self.run,True)
            except (ValueError,RuntimeError,NodeNameNonExistentError) as error:
                if str(error)!=self.pending:self.pending=str(error);print('ABRUPT_PENDING '+self.pending,flush=True)
                return
            if not self.entities.start(1) or not self.entities.complete(1):return
            self.before={'snapshot':snapshot,'data':self.entities.summary(1)}
            write_json(self.root,'abrupt_before.json',self.before);print('ABRUPT_BEFORE '+json.dumps(self.before),flush=True)
            write_json(self.root,'hidden_source.json',{'run_id':self.run,'nonce':self.nonce,'role':self.role,'ready':True})
        if self.armed is None:
            if not self.marker('hidden_cli.go'):return
            self.armed={'run_id':self.run,'nonce':self.nonce,'role':self.role,'armed_ns':time.monotonic_ns()}
            write_json(self.root,'abrupt_armed.json',self.armed);print('ABRUPT_ARM '+json.dumps(self.armed),flush=True)
        if self.removed is None:
            if time.monotonic_ns()-self.armed['armed_ns']>BOUND_NS:raise RuntimeError('victim graph did not withdraw within 5 seconds')
            p=self.root/'victim.status.json'
            if not p.exists():return
            try:snapshot=collect(self.observer,self.run,False)
            except (ValueError,RuntimeError,NodeNameNonExistentError):return
            ready={k:c.service_is_ready() for k,c in self.entities.clients.items()}
            if ready!={'survivor':True,'victim':False}:return
            removed=time.monotonic_ns();validate_pair(self.before['snapshot'],snapshot,self.run)
            validate_kill(json.loads(p.read_bytes()),self.armed['armed_ns'],removed)
            self.removed={'snapshot':snapshot,'ready':ready,'removed_ns':removed}
        if self.after is None:
            if not self.entities.start(2) or not self.entities.complete(2):return
            self.after={**self.removed,'data':self.entities.summary(2),'armed_ns':self.armed['armed_ns']}
            write_json(self.root,'abrupt_after.json',self.after);print('ABRUPT_AFTER '+json.dumps(self.after),flush=True)
            (self.root/'abrupt_after.ready').write_text(self.nonce+'\n')
        if not self.marker('hidden_source.stop'):return
        self.close();final=empty(self.observer,self.run)
        if final!={'nodes':[],'topics':{},'services':{},'parameter_event_owners':[]}:return
        value={'run_id':self.run,'nonce':self.nonce,'role':self.role,'before':self.before,'after':self.after,'final':final}
        write_json(self.root,'abrupt_graph.json',value);print('ABRUPT_GRAPH_RESULT '+json.dumps(value),flush=True)
        (self.root/'hidden_source.done').write_text(self.nonce+'\n');self.finished=True
