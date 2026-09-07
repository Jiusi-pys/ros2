"""Initialize a new ROS context only after immutable source-ready evidence."""
import hashlib
import json
import os
from pathlib import Path
import sys
import time

root=Path(sys.argv[1]);run,role,nonce=sys.argv[2:5]
if root!=Path('/data/local/tmp/ros2/.mdds-owned-runs')/run:raise ValueError('wrong late observer root')
if (root/'late_observer.go').read_text().strip()!=nonce:raise ValueError('late observer release differs')
own=(root/'late_source.json').read_bytes();peer=(root/'late_peer_source.json').read_bytes()
own_value=json.loads(own);peer_value=json.loads(peer)
if own_value['nonce']!=nonce or peer_value['nonce']!=nonce or peer_value['role']==role:raise ValueError('late source readiness differs')
started=time.monotonic_ns()
if started<=own_value['ready_ns']:raise ValueError('observer started before source was ready')
import rclpy
from rclpy.signals import SignalHandlerOptions
from late_graph_snapshot import collect
from broker_local_ros_probe import provenance
from board_graph_ownership import process_start
initialized=time.monotonic_ns()
rclpy.init(args=[],signal_handler_options=SignalHandlerOptions.NO)
node=rclpy.create_node('observer',namespace='/late_observer_'+run+'/'+role,start_parameter_services=False,enable_rosout=False)
try:
    hashes=json.loads((root/'late_graph_hashes.json').read_bytes())['hashes']
    deadline=time.monotonic()+20;last_error=''
    while time.monotonic()<deadline:
        try:
            snapshot=collect(node,run,peer_value['role'],hashes)
            if snapshot!=peer_value['snapshot']:raise ValueError('late snapshot differs from established source snapshot')
            break
        except (RuntimeError,ValueError) as error:
            last_error=str(error);rclpy.spin_once(node,timeout_sec=.05)
    else:raise RuntimeError('late graph did not converge: '+last_error)
    value={'run_id':run,'nonce':nonce,'role':role,'pid':os.getpid(),'start':process_start(os.getpid()),
           'started_ns':started,'initialized_ns':initialized,'completed_ns':time.monotonic_ns(),
           'own_source_sha256':hashlib.sha256(own).hexdigest(),'peer_source_sha256':hashlib.sha256(peer).hexdigest(),
           'snapshot':snapshot,'provenance':provenance(str(root/'lib/libmdds.so'),str(root/'python'),str(root/'rclpy_package.json'),hashlib.sha256((root/'rclpy_package.json').read_bytes()).hexdigest())}
    (root/'late_observer.json').write_text(json.dumps(value)+'\n')
    print('LATE_OBSERVER_RESULT '+json.dumps(value),flush=True)
finally:
    node.destroy_node();rclpy.shutdown()
