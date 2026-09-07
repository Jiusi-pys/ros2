"""Observe old peer withdrawal and new rich peer identities across an SDK cycle."""
import json
from abrupt_graph_contract import write_json
from cycle_graph_contract import collect
from peer_restart_contract import scope,validate_snapshot,validate_recovery

class PeerRestartGraph:
    def __init__(self,root,run,nonce,role,node):
        self.root,self.run,self.nonce,self.role,self.node=root,run,nonce,role,node
        self.before=None;self.paused=None;self.gone=False;self.new=None;self.restored=None;self.finished=False;self.pending=None
        self.hashes=json.loads((root/'late_graph_hashes.json').read_bytes())['hashes']
    def emit(self,stage,snapshot):
        value={'run_id':self.run,'nonce':self.nonce,'role':self.role,'snapshot':snapshot}
        write_json(self.root,'reconnect.peer_'+stage+'.json',value);print('PEER_GRAPH_'+stage.upper()+' '+json.dumps(value),flush=True)
    def snapshot(self,generation,roles):
        value=collect(self.node,scope(self.run),self.before)
        validate_snapshot(value,self.run,generation,roles)
        for e in value['endpoints']:
            if e['hash']!=self.hashes[e['type']]:raise ValueError('peer generated type hash differs')
        return value
    def tick(self):
        if self.finished or not (self.root/'phase1.done').exists():return
        from rclpy.node import NodeNameNonExistentError
        try:
            if self.before is None:
                if not (self.root/'reconnect.peer_1.ready.json').exists():return
                self.before=self.snapshot(1,('A','B'));self.emit('before',self.before);return
            if self.paused is None:
                if not (self.root/'reconnect.paused.json').exists():return
                self.paused=self.snapshot(1,(self.role,));self.emit('paused',self.paused);return
            if not self.gone:
                if not (self.root/'reconnect.peer_1.status.json').exists():return
                value=self.snapshot(1,());self.emit('old_gone',value);self.gone=True;return
            if self.new is None:
                p=self.root/'reconnect.peer_2.created.json'
                if not p.exists():return
                value=self.snapshot(2,(self.role,));old={tuple(v['gid']) for v in self.before['endpoints']}
                if any(tuple(v['gid']) in old for v in value['endpoints']):raise ValueError('old peer GID reused locally')
                self.new=value;self.emit('new_local',value);return
            if self.restored is None:
                if not (self.root/'reconnect.restored.json').exists() or not (self.root/'reconnect.peer_2.ready.json').exists():return
                value=self.snapshot(2,('A','B'));validate_recovery(self.before,value,self.run)
                self.restored=value;self.emit('restored',value);return
            if not (self.root/'reconnect.peer_2.status.json').exists():return
            value=self.snapshot(2,());self.emit('final',value);self.finished=True
        except (ValueError,NodeNameNonExistentError) as error:
            if str(error)!=self.pending:self.pending=str(error);print('PEER_GRAPH_PENDING '+self.pending,flush=True)

class CombinedCycleProbes:
    def __init__(self,*probes):self.probes=probes
    def tick(self):
        for probe in self.probes:probe.tick()
    def close(self):
        for probe in self.probes:
            if hasattr(probe,'close'):probe.close()
