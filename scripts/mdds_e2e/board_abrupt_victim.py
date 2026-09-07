"""Remain alive after verified peer traffic until the owned parent sends SIGKILL."""
import hashlib
import json
import os
from pathlib import Path
import sys
import time
from abrupt_graph_contract import scope,base,write_json
from abrupt_entities import Entities
from board_graph_ownership import process_start
from broker_local_ros_probe import provenance

root=Path(sys.argv[1]);run,role,nonce=sys.argv[2:5]
if root!=Path('/data/local/tmp/ros2/.mdds-owned-runs')/run or (root/'hidden_source.go').read_text().strip()!=nonce:raise ValueError('wrong abrupt victim root/barrier')
import rclpy
from rclpy.context import Context
from rclpy.node import Node
from rclpy.executors import SingleThreadedExecutor
from rclpy.signals import SignalHandlerOptions
context=Context();rclpy.init(context=context,signal_handler_options=SignalHandlerOptions.NO)
node=Node('victim_'+role,namespace=scope(run),context=context,start_parameter_services=False,enable_rosout=False)
executor=SingleThreadedExecutor(context=context);executor.add_node(node);entities=Entities(node,run,nonce,role,'victim')
ready=False;deadline=time.monotonic()+120
try:
    while time.monotonic()<deadline:
        executor.spin_once(timeout_sec=.005)
        if not ready and entities.start(1) and entities.complete(1):
            infos=node.get_publishers_info_by_topic(base(run,role,'victim')+'/out')
            if len(infos)!=1:raise ValueError('victim own publisher identity missing')
            value={'run_id':run,'nonce':nonce,'role':role,'pid':os.getpid(),'start':process_start(os.getpid()),'gid':list(infos[0].endpoint_gid),'data':entities.summary(1),
                   'provenance':provenance(str(root/'lib/libmdds.so'),str(root/'python'),str(root/'rclpy_package.json'),hashlib.sha256((root/'rclpy_package.json').read_bytes()).hexdigest())}
            write_json(root,'victim.ready.json',value);print('ABRUPT_VICTIM_READY '+json.dumps(value),flush=True);ready=True
    raise RuntimeError('victim was never killed')
finally:
    (root/'victim.graceful_shutdown').write_text(nonce+'\n')
    executor.remove_node(node);entities.close();executor.shutdown();context.shutdown();context.destroy()
