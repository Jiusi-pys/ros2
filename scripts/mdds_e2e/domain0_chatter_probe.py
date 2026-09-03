#!/usr/bin/env python3
"""Exact production-domain-0 /chatter probe used by the standalone smoke gate.

The probe deliberately accepts only ``/chatter``.  It emits a compact,
machine-readable result for one directed leg:

  PC CycloneDDS -- mdds_gateway(A) -- DSoftBus/MDDS -- board B

Payloads are ``std_msgs/msg/ByteMultiArray`` values with a versioned binary
header, the complete run token, a direction discriminator, sequence number and
CRC32.  A stale /chatter publication cannot satisfy a current result because it
does not carry the exact token.  This is a production-domain smoke probe, not a
general benchmark or an alternative gateway test runner.
"""

import argparse
import hashlib
import math
import re
import struct
import sys
import time
import zlib


MAGIC = b'D0CH'
VERSION = 1
DIRECTION_PC_TO_B = 1
DIRECTION_B_TO_PC = 2
HEADER = struct.Struct('!4sBBHII')
MAX_TOKEN_BYTES = 160
MIN_PAYLOAD_BYTES = HEADER.size + 1
MAX_PAYLOAD_BYTES = 64 * 1024


def direction_code(direction):
    if direction == 'pc_to_b':
        return DIRECTION_PC_TO_B
    if direction == 'b_to_pc':
        return DIRECTION_B_TO_PC
    raise ValueError(f'unsupported direction: {direction!r}')


def valid_token(value):
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,160}', value):
        raise argparse.ArgumentTypeError(
            'token must contain 1..160 ASCII letters, digits, _ or - characters')
    return value


def positive_count(value):
    number = int(value)
    if not 1 <= number <= 1024:
        raise argparse.ArgumentTypeError('count must be in [1, 1024]')
    return number


def payload_size(value):
    number = int(value)
    if not MIN_PAYLOAD_BYTES <= number <= MAX_PAYLOAD_BYTES:
        raise argparse.ArgumentTypeError(
            f'payload bytes must be in [{MIN_PAYLOAD_BYTES}, {MAX_PAYLOAD_BYTES}]')
    return number


def finite_timeout(value):
    number = float(value)
    if not math.isfinite(number) or not 0.0 < number <= 180.0:
        raise argparse.ArgumentTypeError('timeout must be finite and in (0, 180] seconds')
    return number


def positive_rate(value):
    number = int(value)
    if not 1 <= number <= 100:
        raise argparse.ArgumentTypeError('rate must be in [1, 100] Hz')
    return number


def deterministic_body(token_bytes, direction, sequence, length):
    """Return a per-sample deterministic body for a real full-body CRC check."""
    seed = (token_bytes + b'|' + direction.encode('ascii') + b'|' +
            str(sequence).encode('ascii'))
    digest = hashlib.sha256(seed).digest()
    return (digest * ((length + len(digest) - 1) // len(digest)))[:length]


def encode_sample(token, direction, sequence, total_size):
    token_bytes = token.encode('ascii')
    if len(token_bytes) > MAX_TOKEN_BYTES:
        raise ValueError('token is too long')
    if total_size < HEADER.size + len(token_bytes):
        raise ValueError('payload is too small for the complete token')
    body = deterministic_body(
        token_bytes, direction, sequence, total_size - HEADER.size - len(token_bytes))
    header = HEADER.pack(
        MAGIC, VERSION, direction_code(direction), len(token_bytes), sequence,
        zlib.crc32(body))
    return header + token_bytes + body


def decode_sample(data, expected_token, expected_direction, expected_size):
    """Classify a received payload without treating foreign traffic as ours."""
    if len(data) < HEADER.size:
        return 'foreign', None
    magic, version, wire_direction, token_len, sequence, body_crc = HEADER.unpack_from(data)
    if magic != MAGIC:
        return 'foreign', None
    if version != VERSION or token_len > MAX_TOKEN_BYTES:
        return 'malformed', None
    if len(data) != expected_size or len(data) < HEADER.size + token_len:
        return 'malformed', None
    token_end = HEADER.size + token_len
    try:
        token = data[HEADER.size:token_end].decode('ascii')
    except UnicodeDecodeError:
        return 'malformed', None
    if token != expected_token:
        return 'token_mismatch', None
    if wire_direction != direction_code(expected_direction):
        return 'direction_mismatch', None
    body = data[token_end:]
    if zlib.crc32(body) != body_crc:
        return 'crc', sequence
    expected_body = deterministic_body(token.encode('ascii'), expected_direction,
                                      sequence, len(body))
    if body != expected_body:
        return 'body_mismatch', sequence
    return 'ok', sequence


def normalize_bytes(values):
    """Normalize the OpenHarmony rclpy sequence<uint8> representation."""
    if isinstance(values, bytes):
        return values
    if isinstance(values, (bytearray, memoryview)):
        return bytes(values)
    result = bytearray()
    for value in values:
        if isinstance(value, int):
            result.append(value & 0xff)
        else:
            result += bytes(value)
    return bytes(result)


class ReceiveStats:
    def __init__(self):
        self.received = 0
        self.expected_sequence = 0
        self.lost = 0
        self.reorder = 0
        self.crc = 0
        self.body_mismatch = 0
        self.token_mismatch = 0
        self.direction_mismatch = 0
        self.malformed = 0
        self.foreign = 0
        self.last_accepted = None

    def accept(self, status, sequence):
        if status == 'foreign':
            self.foreign += 1
            return
        if status == 'token_mismatch':
            self.token_mismatch += 1
            return
        if status == 'direction_mismatch':
            self.direction_mismatch += 1
            return
        if status == 'malformed':
            self.malformed += 1
            return
        if status == 'crc':
            self.crc += 1
            return
        if status == 'body_mismatch':
            self.body_mismatch += 1
            return
        if status != 'ok' or sequence is None:
            self.malformed += 1
            return
        self.received += 1
        self.last_accepted = time.monotonic()
        if sequence < self.expected_sequence:
            self.reorder += 1
        elif sequence > self.expected_sequence:
            self.lost += sequence - self.expected_sequence
            self.expected_sequence = sequence + 1
        else:
            self.expected_sequence += 1

    def exact(self, expected):
        return (
            self.received == expected and self.lost == 0 and self.reorder == 0 and
            self.crc == 0 and self.body_mismatch == 0 and self.token_mismatch == 0 and
            self.direction_mismatch == 0 and self.malformed == 0 and self.foreign == 0)


def init_rclpy():
    import rclpy
    from rclpy.signals import SignalHandlerOptions
    rclpy.init(signal_handler_options=SignalHandlerOptions.NO)
    return rclpy


def make_qos():
    from rclpy.qos import DurabilityPolicy, HistoryPolicy, QoSProfile, ReliabilityPolicy
    return QoSProfile(
        depth=32,
        reliability=ReliabilityPolicy.RELIABLE,
        history=HistoryPolicy.KEEP_LAST,
        durability=DurabilityPolicy.VOLATILE,
    )


def publish(args):
    rclpy = init_rclpy()
    from std_msgs.msg import ByteMultiArray

    node = rclpy.create_node('d0_chatter_pub')
    publisher = node.create_publisher(ByteMultiArray, args.topic, make_qos())
    sent = 0
    matched = 0
    error = ''
    try:
        deadline = time.monotonic() + args.match_timeout_s
        while rclpy.ok() and time.monotonic() < deadline:
            matched = publisher.get_subscription_count()
            if matched > 1:
                error = f'unexpected_subscription_count_{matched}'
                break
            if matched == 1:
                break
            rclpy.spin_once(node, timeout_sec=0.1)
        if not error and matched != 1:
            error = f'no_exact_match_count_{matched}'
        if not error:
            settle_deadline = time.monotonic() + args.settle_ms / 1000.0
            while rclpy.ok() and time.monotonic() < settle_deadline:
                rclpy.spin_once(node, timeout_sec=min(0.1, settle_deadline - time.monotonic()))
                matched = publisher.get_subscription_count()
                if matched != 1:
                    error = f'match_changed_to_{matched}'
                    break
        if not error:
            period = 1.0 / args.rate_hz
            next_publish = time.monotonic()
            for sequence in range(args.count):
                payload = encode_sample(args.token, args.direction, sequence, args.payload_bytes)
                message = ByteMultiArray()
                message.data = payload
                publisher.publish(message)
                sent += 1
                print(
                    f'D0_CHATTER_PUB_PROGRESS role={args.role} direction={args.direction} '
                    f'token={args.token} sent={sent}/{args.count}', flush=True)
                next_publish += period
                while rclpy.ok() and time.monotonic() < next_publish:
                    rclpy.spin_once(node, timeout_sec=min(0.05, next_publish - time.monotonic()))
            flush_deadline = time.monotonic() + args.flush_s
            while rclpy.ok() and time.monotonic() < flush_deadline:
                rclpy.spin_once(node, timeout_sec=min(0.1, flush_deadline - time.monotonic()))
        result = 'PASS' if not error and sent == args.count and matched == 1 else 'FAIL'
        print(
            f'D0_CHATTER_PUB_RESULT role={args.role} direction={args.direction} '
            f'token={args.token} sent={sent}/{args.count} match_count={matched} '
            f'error={error or "none"} result={result}', flush=True)
        return 0 if result == 'PASS' else 1
    except Exception as exc:
        print(
            f'D0_CHATTER_PUB_RESULT role={args.role} direction={args.direction} '
            f'token={args.token} sent={sent}/{args.count} match_count={matched} '
            f'error=exception_{type(exc).__name__} result=FAIL', flush=True)
        return 1
    finally:
        if rclpy.ok():
            node.destroy_node()
            rclpy.shutdown()


def subscribe(args):
    rclpy = init_rclpy()
    from std_msgs.msg import ByteMultiArray

    node = rclpy.create_node('d0_chatter_sub')
    stats = ReceiveStats()
    first_data_deadline = time.monotonic() + args.receive_timeout_s
    quiet_deadline = None

    def callback(message):
        data = normalize_bytes(message.data)
        status, sequence = decode_sample(
            data, args.token, args.direction, args.payload_bytes)
        stats.accept(status, sequence)
        if status != 'foreign':
            print(
                f'D0_CHATTER_SUB_PROGRESS role={args.role} direction={args.direction} '
                f'token={args.token} status={status} received={stats.received}/{args.count} '
                f'expected_sequence={stats.expected_sequence}', flush=True)

    subscription = node.create_subscription(ByteMultiArray, args.topic, callback, make_qos())
    del subscription  # node retains the entity; this makes the ownership explicit for linters.
    print(
        f'D0_CHATTER_SUB_READY role={args.role} direction={args.direction} '
        f'token={args.token} expected={args.count} topic={args.topic}', flush=True)
    error = ''
    try:
        while rclpy.ok():
            rclpy.spin_once(node, timeout_sec=0.1)
            now = time.monotonic()
            if stats.received >= args.count:
                if quiet_deadline is None:
                    quiet_deadline = now + args.quiet_s
                if now >= quiet_deadline:
                    break
            elif now >= first_data_deadline:
                error = 'receive_timeout'
                break
        result = 'PASS' if not error and stats.exact(args.count) else 'FAIL'
        print(
            f'D0_CHATTER_SUB_RESULT role={args.role} direction={args.direction} '
            f'token={args.token} received={stats.received}/{args.count} lost={stats.lost} '
            f'reorder={stats.reorder} crc={stats.crc} body_mismatch={stats.body_mismatch} '
            f'token_mismatch={stats.token_mismatch} direction_mismatch={stats.direction_mismatch} '
            f'malformed={stats.malformed} foreign={stats.foreign} '
            f'error={error or "none"} result={result}', flush=True)
        return 0 if result == 'PASS' else 1
    except Exception as exc:
        print(
            f'D0_CHATTER_SUB_RESULT role={args.role} direction={args.direction} '
            f'token={args.token} received={stats.received}/{args.count} lost={stats.lost} '
            f'reorder={stats.reorder} crc={stats.crc} body_mismatch={stats.body_mismatch} '
            f'token_mismatch={stats.token_mismatch} direction_mismatch={stats.direction_mismatch} '
            f'malformed={stats.malformed} foreign={stats.foreign} '
            f'error=exception_{type(exc).__name__} result=FAIL', flush=True)
        return 1
    finally:
        if rclpy.ok():
            node.destroy_node()
            rclpy.shutdown()


def self_test():
    token = 'd0_self_test_123'
    encoded = encode_sample(token, 'pc_to_b', 7, 128)
    status, sequence = decode_sample(encoded, token, 'pc_to_b', 128)
    if status != 'ok' or sequence != 7:
        raise AssertionError(f'round trip failed: {status!r}, {sequence!r}')
    wrong = bytearray(encoded)
    wrong[-1] ^= 0x01
    status, _ = decode_sample(bytes(wrong), token, 'pc_to_b', 128)
    if status != 'crc':
        raise AssertionError(f'CRC corruption not rejected: {status!r}')
    status, _ = decode_sample(encoded, 'different_token', 'pc_to_b', 128)
    if status != 'token_mismatch':
        raise AssertionError(f'token mismatch not rejected: {status!r}')
    print('D0_CHATTER_PROBE_SELF_TEST PASS')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true',
                        help='exercise the pure wire parser without ROS or a device')
    parser.add_argument('--role', choices=['pc', 'board_b'])
    parser.add_argument('--mode', choices=['pub', 'sub'])
    parser.add_argument('--direction', choices=['pc_to_b', 'b_to_pc'])
    parser.add_argument('--token', type=valid_token)
    parser.add_argument('--topic', default='/chatter')
    parser.add_argument('--count', type=positive_count, default=10)
    parser.add_argument('--payload-bytes', type=payload_size, default=512)
    parser.add_argument('--rate-hz', type=positive_rate, default=5)
    parser.add_argument('--match-timeout-s', type=finite_timeout, default=45.0)
    parser.add_argument('--receive-timeout-s', type=finite_timeout, default=45.0)
    parser.add_argument('--settle-ms', type=int, default=3000)
    parser.add_argument('--flush-s', type=finite_timeout, default=3.0)
    parser.add_argument('--quiet-s', type=finite_timeout, default=2.0)
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.topic != '/chatter':
        parser.error('this production smoke probe is intentionally fixed to /chatter')
    for required in ('role', 'mode', 'direction', 'token'):
        if getattr(args, required) is None:
            parser.error(f'--{required.replace("_", "-")} is required unless --self-test is used')
    if args.payload_bytes < HEADER.size + len(args.token.encode('ascii')):
        parser.error('payload bytes is too small for this run token')
    if not 0 <= args.settle_ms <= 120000:
        parser.error('settle ms must be in [0, 120000]')
    return publish(args) if args.mode == 'pub' else subscribe(args)


if __name__ == '__main__':
    sys.exit(main())
