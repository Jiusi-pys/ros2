#!/usr/bin/env python3
"""Simultaneous, exact bidirectional ByteMultiArray gateway endurance probe.

Each endpoint owns one publisher and one subscriber on distinct, run-unique
topics.  The process is deliberately a single ROS node so that a successful
result demonstrates that a direction made progress while the opposite
direction was publishing at the same time; it is not two serial one-way tests.

The wire payload has the same full-body CRC contract as ``board_sweep.py``.
Machine-readable ``GW10_*`` records are emitted on every terminal path so the
host runner can require exact N/N delivery, ordering, integrity, and the
absence of directional starvation independently at PC and board B.
"""

import argparse
import math
import os
import re
import stat
import struct
import sys
import time
import zlib


MAGIC = 0x47573130  # "GW10"
HEADER = struct.Struct('<IIQI')
MAX_COUNT = 1_000_000
MAX_SIZE = 8 * 1024 * 1024
MAX_TIMEOUT_S = 900.0
MAX_BARRIER_TIMEOUT_S = 120.0


def as_bytes(data):
    """Normalize the OpenHarmony rclpy sequence<uint8> representation."""
    if isinstance(data, bytes):
        return data
    if isinstance(data, (bytearray, memoryview)):
        return bytes(data)
    out = bytearray()
    for item in data:
        if isinstance(item, int):
            out.append(item & 0xff)
        else:
            out += bytes(item)
    return bytes(out)


class ReceiveStats:
    def __init__(self, start):
        self.received = 0
        self.expected_next = 0
        self.lost = 0
        self.reorder = 0
        self.crc = 0
        self.malformed = 0
        self.last_progress = start
        self.max_silence = 0.0

    def note_progress(self, now):
        self.max_silence = max(self.max_silence, now - self.last_progress)
        self.last_progress = now


def wait_for_barrier_release(release_file, token, timeout_s):
    """Wait for a complete exact-token regular file without a path re-open.

    GW-10 uses this only to hold board B's outbound direction until the PC
    endpoint has created its inbound subscription.  The no-follow descriptor
    check preserves that synchronization boundary even on a shared board.
    """
    expected = f'GW10_BIDIR_RELEASE token={token}\n'.encode('ascii')
    if not hasattr(os, 'O_NOFOLLOW') or not hasattr(os, 'O_NONBLOCK'):
        return False, 'safe_open_flags_unavailable'
    deadline = time.monotonic() + timeout_s
    while True:
        try:
            fd = os.open(release_file, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        except FileNotFoundError:
            fd = None
        except OSError as exc:
            return False, f'open={exc!r}'
        else:
            try:
                if not stat.S_ISREG(os.fstat(fd).st_mode):
                    return False, 'not_regular'
                body = os.read(fd, len(expected) + 1)
            except OSError as exc:
                return False, f'read={exc!r}'
            finally:
                os.close(fd)
            return (True, 'released') if body == expected else (False, 'token_mismatch')
        now = time.monotonic()
        if now >= deadline:
            return False, 'timeout'
        time.sleep(min(0.1, deadline - now))


def make_qos(depth):
    from rclpy.qos import DurabilityPolicy, HistoryPolicy, QoSProfile, ReliabilityPolicy

    return QoSProfile(
        depth=depth,
        reliability=ReliabilityPolicy.RELIABLE,
        history=HistoryPolicy.KEEP_LAST,
        durability=DurabilityPolicy.VOLATILE,
    )


def make_body(topic, sequence, size):
    """Make every sequence's complete body deterministic and CRC-verifiable."""
    body_size = size - HEADER.size
    seed = zlib.crc32(topic.encode('utf-8'))
    unit = struct.pack('<IQ', seed, sequence)
    return (unit * ((body_size + len(unit) - 1) // len(unit)))[:body_size]


def emit_result(args, stats, sent, start, starvation, result):
    now = time.monotonic()
    stats.max_silence = max(stats.max_silence, now - stats.last_progress)
    print(
        'GW10_ENDPOINT_RESULT '
        f'role={args.role} direction_out={args.direction_out} '
        f'direction_in={args.direction_in} sent={sent}/{args.count} '
        f'received={stats.received}/{args.count} lost={stats.lost} '
        f'reorder={stats.reorder} crc={stats.crc} malformed={stats.malformed} '
        f'starvation={1 if starvation else 0} '
        f'max_silence_ms={int(stats.max_silence * 1000)} '
        f'elapsed_ms={int((now - start) * 1000)} result={result}',
        flush=True)


def run(args):
    import rclpy
    from rclpy.signals import SignalHandlerOptions
    from std_msgs.msg import ByteMultiArray

    rclpy.init(signal_handler_options=SignalHandlerOptions.NO)
    node = rclpy.create_node(f'gw10_bidir_{args.role}')
    start = time.monotonic()
    stats = ReceiveStats(start)
    sent = 0
    starvation = False
    completed = False

    def callback(message):
        data = as_bytes(message.data)
        now = time.monotonic()
        if len(data) < HEADER.size:
            stats.malformed += 1
            return
        magic, declared_size, sequence, body_crc = HEADER.unpack_from(data)
        if magic != MAGIC or declared_size != args.size or len(data) != declared_size:
            stats.malformed += 1
            return
        stats.received += 1
        if zlib.crc32(data[HEADER.size:]) != body_crc:
            stats.crc += 1
        if sequence < stats.expected_next:
            stats.reorder += 1
        else:
            if sequence > stats.expected_next:
                stats.lost += sequence - stats.expected_next
            stats.expected_next = sequence + 1
        stats.note_progress(now)
        if stats.received % max(1, args.count // 8) == 0:
            print(
                f'GW10_SUB_PROGRESS role={args.role} direction={args.direction_in} '
                f'received={stats.received}/{args.count} expected_next={stats.expected_next}',
                flush=True)

    subscriptions = []
    try:
        publisher = node.create_publisher(ByteMultiArray, args.pub_topic, make_qos(args.depth))
        # Keep an explicit strong reference through node destruction; this
        # avoids tying the probe's lifetime to private rclpy node ownership.
        subscriptions.append(node.create_subscription(
            ByteMultiArray, args.sub_topic, callback, make_qos(args.depth)))
        print(
            f'GW10_ENDPOINT_READY role={args.role} direction_out={args.direction_out} '
            f'direction_in={args.direction_in} pub_topic={args.pub_topic} '
            f'sub_topic={args.sub_topic} count={args.count} size={args.size} '
            f'rate_hz={args.rate_hz} depth={args.depth}',
            flush=True)
        match_start = time.monotonic()
        while rclpy.ok() and publisher.get_subscription_count() < 1:
            if time.monotonic() - match_start >= args.match_timeout_s:
                print(
                    f'GW10_ENDPOINT_ERROR role={args.role} reason=no_match '
                    f'local_subs={publisher.get_subscription_count()} '
                    f'timeout_ms={int(args.match_timeout_s * 1000)}',
                    flush=True)
                emit_result(args, stats, sent, start, False, 'FAIL')
                return 1
            rclpy.spin_once(node, timeout_sec=0.1)
        if not rclpy.ok():
            print(f'GW10_ENDPOINT_ERROR role={args.role} reason=ros_shutdown_before_match', flush=True)
            emit_result(args, stats, sent, start, False, 'FAIL')
            return 1
        print(
            f'GW10_ENDPOINT_MATCHED role={args.role} '
            f'local_subs={publisher.get_subscription_count()} '
            f'elapsed_ms={int((time.monotonic() - match_start) * 1000)}',
            flush=True)
        if args.barrier_release_file:
            print(
                f'GW10_ENDPOINT_BARRIER_READY role={args.role} '
                f'token={args.barrier_token} '
                f'local_subs={publisher.get_subscription_count()}',
                flush=True)
            released, reason = wait_for_barrier_release(
                args.barrier_release_file, args.barrier_token, args.barrier_timeout_s)
            if not released:
                print(
                    f'GW10_ENDPOINT_ERROR role={args.role} reason=barrier_{reason} '
                    f'token={args.barrier_token}',
                    flush=True)
                emit_result(args, stats, sent, start, False, 'FAIL')
                return 1
            print(
                f'GW10_ENDPOINT_BARRIER_RELEASED role={args.role} '
                f'token={args.barrier_token}',
                flush=True)
        if args.settle_ms:
            print(f'GW10_ENDPOINT_SETTLE role={args.role} ms={args.settle_ms}', flush=True)
            settle_deadline = time.monotonic() + args.settle_ms / 1000.0
            while rclpy.ok() and time.monotonic() < settle_deadline:
                rclpy.spin_once(node, timeout_sec=min(0.1, settle_deadline - time.monotonic()))
            if not rclpy.ok():
                print(f'GW10_ENDPOINT_ERROR role={args.role} reason=ros_shutdown_during_settle', flush=True)
                emit_result(args, stats, sent, start, False, 'FAIL')
                return 1

        start = time.monotonic()
        stats.last_progress = start
        next_publish = start
        period = 1.0 / args.rate_hz
        while rclpy.ok():
            now = time.monotonic()
            rclpy.spin_once(node, timeout_sec=0.0)
            now = time.monotonic()
            if sent < args.count and now >= next_publish:
                body = make_body(args.pub_topic, sent, args.size)
                message = ByteMultiArray()
                message.data = HEADER.pack(MAGIC, args.size, sent, zlib.crc32(body)) + body
                publisher.publish(message)
                sent += 1
                if sent % max(1, args.count // 8) == 0 or sent == args.count:
                    print(
                        f'GW10_PUB_PROGRESS role={args.role} direction={args.direction_out} '
                        f'sent={sent}/{args.count}',
                        flush=True)
                # Do not issue a catch-up burst after a delayed callback: a
                # gateway starvation test must retain a bounded offered rate.
                next_publish = max(next_publish + period, time.monotonic() + period)

            now = time.monotonic()
            if sent == args.count and stats.received >= args.count:
                completed = True
                break
            silence = now - stats.last_progress
            if sent > 0 and stats.received < args.count and silence >= args.starvation_timeout_s:
                starvation = True
                print(
                    f'GW10_STARVATION role={args.role} direction={args.direction_in} '
                    f'received={stats.received}/{args.count} '
                    f'silence_ms={int(silence * 1000)} '
                    f'timeout_ms={int(args.starvation_timeout_s * 1000)}',
                    flush=True)
                break
            if now - start >= args.overall_timeout_s:
                print(
                    f'GW10_ENDPOINT_ERROR role={args.role} reason=overall_timeout '
                    f'sent={sent}/{args.count} received={stats.received}/{args.count} '
                    f'timeout_ms={int(args.overall_timeout_s * 1000)}',
                    flush=True)
                break
            sleep_for = 0.01
            if sent < args.count:
                sleep_for = min(sleep_for, max(0.0, next_publish - time.monotonic()))
            if sleep_for > 0:
                time.sleep(sleep_for)

        exact = (
            completed and sent == args.count and stats.received == args.count and
            stats.lost == 0 and stats.reorder == 0 and stats.crc == 0 and
            stats.malformed == 0 and not starvation)
        emit_result(args, stats, sent, start, starvation, 'PASS' if exact else 'FAIL')
        return 0 if exact else 1
    except Exception as exc:  # Print a durable machine marker before teardown.
        print(f'GW10_ENDPOINT_ERROR role={args.role} reason=exception error={exc!r}', flush=True)
        emit_result(args, stats, sent, start, starvation, 'FAIL')
        return 1
    finally:
        if rclpy.ok():
            node.destroy_node()
            rclpy.shutdown()
        subscriptions.clear()


def valid_topic(value):
    if not re.fullmatch(r'/[A-Za-z0-9_/]+', value):
        raise argparse.ArgumentTypeError('topic must be an absolute ROS name using A-Z a-z 0-9 _ /')
    return value


def valid_barrier_path(value):
    if not value.startswith('/') or not re.fullmatch(r'/[A-Za-z0-9._/-]+', value):
        raise argparse.ArgumentTypeError('barrier path must be an absolute safe path')
    components = value.split('/')[1:]
    if any(component in ('', '.', '..') for component in components):
        raise argparse.ArgumentTypeError('barrier path must not contain empty, . or .. components')
    return value


def positive_int(value):
    number = int(value)
    if not 1 <= number <= MAX_COUNT:
        raise argparse.ArgumentTypeError(f'value must be in [1, {MAX_COUNT}]')
    return number


def finite_timeout(value):
    number = float(value)
    if not math.isfinite(number) or not 0.0 < number <= MAX_TIMEOUT_S:
        raise argparse.ArgumentTypeError(f'timeout must be finite and in (0, {MAX_TIMEOUT_S}]')
    return number


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--role', required=True, choices=['pc', 'board_b'])
    parser.add_argument('--direction-out', required=True, choices=['pc_to_b', 'b_to_pc'])
    parser.add_argument('--direction-in', required=True, choices=['pc_to_b', 'b_to_pc'])
    parser.add_argument('--pub-topic', required=True, type=valid_topic)
    parser.add_argument('--sub-topic', required=True, type=valid_topic)
    parser.add_argument('--count', required=True, type=positive_int)
    parser.add_argument('--size', required=True, type=int)
    parser.add_argument('--rate-hz', required=True, type=positive_int)
    parser.add_argument('--depth', default=1024, type=positive_int)
    parser.add_argument('--match-timeout-s', default=60.0, type=finite_timeout)
    parser.add_argument('--settle-ms', default=3000, type=int)
    parser.add_argument('--starvation-timeout-s', default=45.0, type=finite_timeout)
    parser.add_argument('--overall-timeout-s', default=360.0, type=finite_timeout)
    # ``argparse`` applies a ``type=`` converter to a string default too.  An
    # empty string therefore must not be used here: the PC endpoint has no
    # release barrier and would reject its own omitted optional argument before
    # it can establish the inbound subscription.  ``None`` preserves the
    # absent state and is also unambiguous in the later barrier contract.
    parser.add_argument('--barrier-release-file', default=None, type=valid_barrier_path)
    parser.add_argument('--barrier-token', default='')
    parser.add_argument('--barrier-timeout-s', default=0.0, type=float)
    args = parser.parse_args(argv)
    if args.direction_out == args.direction_in:
        parser.error('direction-out and direction-in must be distinct')
    if args.pub_topic == args.sub_topic:
        parser.error('pub-topic and sub-topic must be distinct')
    if not HEADER.size <= args.size <= MAX_SIZE:
        parser.error(f'--size must be in [{HEADER.size}, {MAX_SIZE}]')
    if not 0 <= args.settle_ms <= 120_000:
        parser.error('--settle-ms must be in [0, 120000]')
    minimum_runtime = args.count / args.rate_hz
    if args.overall_timeout_s <= minimum_runtime:
        parser.error('--overall-timeout-s must exceed count/rate-hz')
    barrier_args_present = bool(
        args.barrier_release_file or args.barrier_token or args.barrier_timeout_s)
    if barrier_args_present:
        if args.role != 'board_b':
            parser.error('the release barrier is reserved for the board_b endpoint')
        if not args.barrier_release_file or not args.barrier_token:
            parser.error('barrier requires --barrier-release-file and --barrier-token')
        if not re.fullmatch(r'[A-Za-z0-9_-]{1,200}', args.barrier_token):
            parser.error('--barrier-token must use 1..200 A-Z a-z 0-9 _ - characters')
        if not math.isfinite(args.barrier_timeout_s) or not 0 < args.barrier_timeout_s <= MAX_BARRIER_TIMEOUT_S:
            parser.error('--barrier-timeout-s must be finite and in (0, 120]')
    return args


def main():
    return run(parse_args())


if __name__ == '__main__':
    sys.exit(main())
