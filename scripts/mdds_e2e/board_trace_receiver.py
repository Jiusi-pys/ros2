"""Exact peer publication receipts while trace capture is toggled."""
import json

PHASES = ('active', 'paused', 'resumed', 'stopped', 'interactive')


def payload(run, nonce, role, phase): return '|'.join((run, nonce, role, phase))


class TraceReceiver:
    def __init__(self, root, run, nonce, board, peer, node):
        from std_msgs.msg import String
        from rclpy.qos import QoSProfile, ReliabilityPolicy
        self.received = []
        qos = QoSProfile(depth=16, reliability=ReliabilityPolicy.RELIABLE)
        ack = node.create_publisher(String, '/trace_' + run + '/' + peer + '/ack', qos)
        def callback(message):
            if len(self.received) >= len(PHASES): raise ValueError('duplicate trace publication')
            expected = payload(run, nonce, peer, PHASES[len(self.received)])
            if message.data != expected: raise ValueError('trace publication content/order differs')
            self.received.append(message.data)
            value = {'run_id': run, 'nonce': nonce, 'board': board, 'peer_role': peer, 'received': self.received}
            tmp = root / 'trace_received.pending'
            tmp.write_text(json.dumps(value) + '\n'); tmp.replace(root / 'trace_received.json')
            print('TRACE_PEER_RX ' + json.dumps(value), flush=True)
            ack.publish(message)
        self.subscription = node.create_subscription(String, '/trace_' + run + '/' + peer + '/out', callback, qos)
        self.ack = ack

    def tick(self): pass
