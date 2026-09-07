"""One rich peer generation; only generation 2 exits gracefully on request."""
import hashlib
import json
import os
from pathlib import Path
import sys
import time
from abrupt_graph_contract import write_json
from peer_restart_contract import scope,path,payloads
from board_graph_ownership import process_start
from broker_local_ros_probe import provenance

root=Path(sys.argv[1]);run,role,nonce=sys.argv[2:5];generation=int(sys.argv[5])
if root!=Path('/data/local/tmp/ros2/.mdds-owned-runs')/run or generation not in (1,2) or (root/'peer_restart.enabled').read_text().strip()!=nonce:raise ValueError('invalid peer generation/root')
import rclpy
from rclpy.context import Context
from rclpy.node import Node
from rclpy.executors import SingleThreadedExecutor
from rclpy.signals import SignalHandlerOptions
from rclpy.utilities import get_rmw_implementation_identifier
from rclpy.action import ActionClient,ActionServer
from std_msgs.msg import String
from example_interfaces.srv import AddTwoInts
from example_interfaces.action import Fibonacci

context=Context();rclpy.init(args=['--ros-args','--enclave',f'/peer/{role}/g{generation}'],context=context,signal_handler_options=SignalHandlerOptions.NO)
if get_rmw_implementation_identifier()!='rmw_mdds':raise ValueError('wrong peer RMW')
node=Node('peer_'+role,namespace=scope(run),context=context,start_parameter_services=False,enable_rosout=False)
executor=SingleThreadedExecutor(context=context);executor.add_node(node)
peer='B' if role=='A' else 'A';received=[];served=[];actions=[];pub=node.create_publisher(String,path(run,role)+'/out',10)
def receive(message):
    wanted=payloads(run,nonce,peer,generation)
    if len(received)>=3 or message.data!=wanted[len(received)]:raise ValueError('wrong or stale peer-generation payload')
    received.append(message.data);print('RESTART_PEER_RX '+json.dumps({'run_id':run,'nonce':nonce,'role':role,'generation':generation,'data':message.data}),flush=True)
node.create_subscription(String,path(run,peer)+'/out',receive,10)
def serve(request,response):
    if served or request.a!=int(nonce[:7],16)+(1 if peer=='A' else 2) or request.b!=7000+generation:raise ValueError('wrong or repeated peer request')
    response.sum=request.a+request.b
    v={'run_id':run,'nonce':nonce,'role':role,'generation':generation,'a':request.a,'b':request.b,'sum':response.sum};served.append(v);print('RESTART_PEER_SERVER '+json.dumps(v),flush=True)
    return response
node.create_service(AddTwoInts,path(run,role)+'/serve',serve);client=node.create_client(AddTwoInts,path(run,peer)+'/serve')
def action(handle):result=Fibonacci.Result();result.sequence=[0,1];handle.succeed();return result
actions=[ActionServer(node,Fibonacci,path(run,role)+'/action',action),ActionClient(node,Fibonacci,path(run,peer)+'/action')]
identity={'run_id':run,'nonce':nonce,'role':role,'generation':generation,'pid':os.getpid(),'start':process_start(os.getpid()),
          'gid':list(node.get_publishers_info_by_topic(path(run,role)+'/out')[0].endpoint_gid),
          'provenance':provenance(str(root/'lib/libmdds.so'),str(root/'python'),str(root/'rclpy_package.json'),hashlib.sha256((root/'rclpy_package.json').read_bytes()).hexdigest())}
write_json(root,f'reconnect.peer_{generation}.created.json',identity);print('RESTART_PEER_CREATED '+json.dumps(identity),flush=True)
future=None;ready=False;deadline=time.monotonic()+180
try:
    while time.monotonic()<deadline:
        executor.spin_once(timeout_sec=.005)
        if future is None and pub.get_subscription_count()==1 and client.service_is_ready():
            for text in payloads(run,nonce,role,generation):message=String();message.data=text;pub.publish(message)
            request=AddTwoInts.Request();request.a=int(nonce[:7],16)+(1 if role=='A' else 2);request.b=7000+generation;future=client.call_async(request)
        if not ready and future is not None and future.done() and len(received)==3 and len(served)==1:
            result=future.result().sum;expected=int(nonce[:7],16)+(1 if role=='A' else 2)+7000+generation
            if result!=expected:raise ValueError('wrong peer RPC response')
            value={**identity,'received':list(received),'served':served,'rpc_sum':result};write_json(root,f'reconnect.peer_{generation}.ready.json',value);print('RESTART_PEER_READY '+json.dumps(value),flush=True);ready=True
        stop=root/'peer_stop.go'
        if generation==2 and ready and stop.exists():
            if stop.read_text().strip()!=nonce:raise ValueError('wrong peer stop')
            break
    else:raise RuntimeError('peer generation timed out')
finally:
    for value in actions:value.destroy()
    executor.remove_node(node);node.destroy_node();executor.shutdown();context.shutdown();context.destroy()
