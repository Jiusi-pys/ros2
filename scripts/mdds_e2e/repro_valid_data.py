#!/usr/bin/env python3
# Minimal repro for test_subscription_valid_data__rmw_mdds hanging after
# "publishing message #1": single process, one node, pub+sub on the same
# topic, publish/sleep/spin_some loop. faulthandler dumps every thread stack
# after 30 s so the exact blocking call is visible in the output.
import faulthandler
import sys
import time

faulthandler.dump_traceback_later(30, exit=True)

import rclpy
from rclpy.signals import SignalHandlerOptions
from std_msgs.msg import UInt32

TOPIC = 'repro_valid_data'

rclpy.init(signal_handler_options=SignalHandlerOptions.NO)
node = rclpy.create_node('repro_valid_data')
received = []


def cb(m):
    received.append(m.data)
    print(f'RECEIVED {m.data}', flush=True)


node.create_subscription(UInt32, TOPIC, cb, 10)
pub = node.create_publisher(UInt32, TOPIC, 10)
time.sleep(0.5)
for i in range(1, 6):
    print(f'PUBLISH {i}', flush=True)
    msg = UInt32()
    msg.data = i
    pub.publish(msg)
    print(f'PUBLISH-RET {i}', flush=True)
    time.sleep(0.2)
    print(f'SPIN {i}', flush=True)
    rclpy.spin_once(node, timeout_sec=0)
    print(f'SPIN-RET {i}', flush=True)
print('LOOP-DONE', flush=True)
time.sleep(0.5)
rclpy.spin_once(node, timeout_sec=0)
print(f'REPRO-RESULT received={received}', flush=True)
sys.exit(0)
