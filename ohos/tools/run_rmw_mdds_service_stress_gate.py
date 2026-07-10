#!/usr/bin/env python3
"""Run an RK3588A rmw_mdds service stress gate and persist board summaries."""

from __future__ import annotations

import argparse
import json
import os
import shlex
import subprocess
import sys
import tempfile
import textwrap
import time
from pathlib import Path


HELPER = r'''
#!/usr/bin/env python3
import os
import sys
import time

import rclpy
from example_interfaces.srv import AddTwoInts
from rclpy.parameter import Parameter


def shutdown_once():
    try:
        rclpy.shutdown()
    except Exception:
        pass


def make_node(name):
    return rclpy.create_node(
        name,
        enable_rosout=False,
        start_parameter_services=False,
        parameter_overrides=[
            Parameter("start_type_description_service", Parameter.Type.BOOL, False),
        ],
    )


def run_server(service_name, expected, timeout):
    count = {"value": 0}
    rclpy.init()
    node = make_node("rmw_mdds_stress_server")

    def handle(request, response):
        count["value"] += 1
        response.sum = request.a + request.b
        print(f"SERVER_REQ id={request.a} count={count['value']} ts_ns={time.monotonic_ns()}", flush=True)
        return response

    node.create_service(AddTwoInts, service_name, handle)
    deadline = time.monotonic() + timeout
    while count["value"] < expected and time.monotonic() < deadline:
        rclpy.spin_once(node, timeout_sec=0.05)
    drain_deadline = time.monotonic() + float(os.environ.get("STRESS_SERVER_DRAIN_SEC", "3.0"))
    while count["value"] >= expected and time.monotonic() < drain_deadline:
        rclpy.spin_once(node, timeout_sec=0.05)
    print(f"SERVER_DONE requests={count['value']} expected={expected}", flush=True)
    node.destroy_node()
    shutdown_once()
    return 0 if count["value"] >= expected else 1


def wait_for_future(node, future, timeout):
    deadline = time.monotonic() + timeout
    while not future.done() and time.monotonic() < deadline:
        rclpy.spin_once(node, timeout_sec=0.02)
    return future.done()


def call_once(client_id, service_name, timeout):
    client_id_int = int(client_id)
    print(f"CLIENT_STARTED id={client_id}", flush=True)
    rclpy.init()
    node = make_node(f"rmw_mdds_stress_client_{client_id}")
    client = node.create_client(AddTwoInts, service_name)
    print(f"CLIENT_CREATED id={client_id}", flush=True)
    if not client.wait_for_service(timeout_sec=timeout):
        print(f"CLIENT_TIMEOUT id={client_id} phase=wait_for_service", flush=True)
        node.destroy_node()
        shutdown_once()
        return 2
    print(f"CLIENT_WAIT_OK id={client_id}", flush=True)
    request = AddTwoInts.Request()
    request.a = client_id_int
    request.b = 100000
    future = client.call_async(request)
    print(f"CLIENT_SENT id={client_id}", flush=True)
    if not wait_for_future(node, future, timeout):
        print(f"CLIENT_TIMEOUT id={client_id} phase=response", flush=True)
        print(f"CLIENT_DESTROY_START id={client_id}", flush=True)
        node.destroy_node()
        print(f"CLIENT_DESTROY_DONE id={client_id}", flush=True)
        print(f"CLIENT_SHUTDOWN_START id={client_id}", flush=True)
        shutdown_once()
        print(f"CLIENT_SHUTDOWN_DONE id={client_id}", flush=True)
        return 3
    try:
        response = future.result()
    except Exception as exc:  # noqa: BLE001 - board-side diagnostics need type/message.
        print(f"CLIENT_ERROR id={client_id} phase=result type={type(exc).__name__} msg={str(exc)[:160]}", flush=True)
        response = None
    if response is not None and response.sum == client_id_int + 100000:
        print(f"CLIENT_OK id={client_id} sum={response.sum}", flush=True)
        rc = 0
    else:
        actual = None if response is None else response.sum
        print(f"CLIENT_ERROR id={client_id} phase=response_invalid sum={actual}", flush=True)
        rc = 4
    print(f"CLIENT_DESTROY_START id={client_id}", flush=True)
    node.destroy_node()
    print(f"CLIENT_DESTROY_DONE id={client_id}", flush=True)
    print(f"CLIENT_SHUTDOWN_START id={client_id}", flush=True)
    shutdown_once()
    print(f"CLIENT_SHUTDOWN_DONE id={client_id}", flush=True)
    return rc


def run_many_clients(service_name, clients, timeout):
    print("MANY_CLIENTS_STARTED", flush=True)
    rclpy.init()
    node = make_node("rmw_mdds_stress_many_clients")
    client_objs = []
    for i in range(1, clients + 1):
        client = node.create_client(AddTwoInts, service_name)
        client_objs.append((i, client))
        print(f"CLIENT_CREATED id={i}", flush=True)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        ready = sum(1 for _i, client in client_objs if client.service_is_ready())
        if ready == clients:
            break
        rclpy.spin_once(node, timeout_sec=0.05)
    for i, client in client_objs:
        if not client.service_is_ready():
            print(f"CLIENT_TIMEOUT id={i} phase=wait_for_service", flush=True)
            continue
        print(f"CLIENT_WAIT_OK id={i}", flush=True)
        request = AddTwoInts.Request()
        request.a = i
        request.b = 100000
        future = client.call_async(request)
        print(f"CLIENT_SENT id={i}", flush=True)
        client_objs[i - 1] = (i, client, future)
    done = set()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline and len(done) < clients:
        rclpy.spin_once(node, timeout_sec=0.02)
        for entry in client_objs:
            if len(entry) != 3:
                continue
            i, _client, future = entry
            if i in done or not future.done():
                continue
            done.add(i)
            try:
                response = future.result()
            except Exception as exc:  # noqa: BLE001
                print(f"CLIENT_ERROR id={i} phase=result type={type(exc).__name__} msg={str(exc)[:160]}", flush=True)
                continue
            if response.sum == i + 100000:
                print(f"CLIENT_OK id={i} sum={response.sum}", flush=True)
            else:
                print(f"CLIENT_ERROR id={i} phase=response_invalid sum={response.sum}", flush=True)
    for entry in client_objs:
        i = entry[0]
        if len(entry) == 3 and i not in done:
            print(f"CLIENT_TIMEOUT id={i} phase=response", flush=True)
    print("CLIENT_DESTROY_START id=all", flush=True)
    node.destroy_node()
    print("CLIENT_DESTROY_DONE id=all", flush=True)
    print("CLIENT_SHUTDOWN_START id=all", flush=True)
    shutdown_once()
    print("CLIENT_SHUTDOWN_DONE id=all", flush=True)
    return 0 if len(done) == clients else 5


def run_one_client_many(service_name, requests, timeout):
    print("ONE_CLIENT_MANY_STARTED", flush=True)
    rclpy.init()
    node = make_node("rmw_mdds_stress_one_client_many")
    client = node.create_client(AddTwoInts, service_name)
    print("CLIENT_CREATED id=1", flush=True)
    if not client.wait_for_service(timeout_sec=timeout):
        print("CLIENT_TIMEOUT id=1 phase=wait_for_service", flush=True)
        node.destroy_node()
        shutdown_once()
        return 2
    print("CLIENT_WAIT_OK id=1", flush=True)
    futures = []
    for i in range(1, requests + 1):
        request = AddTwoInts.Request()
        request.a = i
        request.b = 100000
        futures.append((i, client.call_async(request)))
        print(f"CLIENT_SENT id={i}", flush=True)
    done = set()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline and len(done) < requests:
        rclpy.spin_once(node, timeout_sec=0.02)
        for i, future in futures:
            if i in done or not future.done():
                continue
            done.add(i)
            try:
                response = future.result()
            except Exception as exc:  # noqa: BLE001
                print(f"CLIENT_ERROR id={i} phase=result type={type(exc).__name__} msg={str(exc)[:160]}", flush=True)
                continue
            if response.sum == i + 100000:
                print(f"CLIENT_OK id={i} sum={response.sum}", flush=True)
            else:
                print(f"CLIENT_ERROR id={i} phase=response_invalid sum={response.sum}", flush=True)
    for i, _future in futures:
        if i not in done:
            print(f"CLIENT_TIMEOUT id={i} phase=response", flush=True)
    print("CLIENT_DESTROY_START id=1", flush=True)
    node.destroy_node()
    print("CLIENT_DESTROY_DONE id=1", flush=True)
    print("CLIENT_SHUTDOWN_START id=1", flush=True)
    shutdown_once()
    print("CLIENT_SHUTDOWN_DONE id=1", flush=True)
    return 0 if len(done) == requests else 5


def main():
    mode = sys.argv[1]
    service_name = sys.argv[2]
    count = int(sys.argv[3])
    timeout = float(sys.argv[4])
    if mode == "server":
        return run_server(service_name, count, timeout)
    if mode == "client_once":
        return call_once(os.environ.get("STRESS_CLIENT_ID", "0"), service_name, timeout)
    if mode == "many_clients_one_process":
        return run_many_clients(service_name, count, timeout)
    if mode == "one_client_many_requests":
        return run_one_client_many(service_name, count, timeout)
    print(f"unknown mode: {mode}", flush=True)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
'''


def run(cmd: list[str], timeout: int | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        cmd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout,
        check=False,
    )


def hdc_ok(status: int, output: str) -> bool:
    bad = ("Connect server failed", "Connect key failed", "No device", "device offline", "[Fail]")
    if any(marker in output for marker in bad):
        return False
    if status == 0:
        return True
    if status in (139, -11) and output.strip():
        return True
    # The host-side hdc wrapper can segfault after a completed transfer and
    # make the enclosing timeout command report failure. Trust the explicit
    # board/hdc success marker in that case.
    return "FileTransfer finish" in output


class Hdc:
    def __init__(self, hdc_bin: str, timeout: int, attempts: int) -> None:
        self.hdc_bin = hdc_bin
        self.timeout = timeout
        self.attempts = attempts

    def shell(self, device: str, command: str) -> str:
        last = ""
        for _attempt in range(1, self.attempts + 1):
            proc = run(
                ["timeout", f"{self.timeout}s", self.hdc_bin, "-t", device, "shell", command],
                timeout=self.timeout + 10,
            )
            last = proc.stdout
            if hdc_ok(proc.returncode, proc.stdout):
                return proc.stdout
            time.sleep(1)
        raise RuntimeError(last)

    def send(self, device: str, local: Path, remote: str) -> str:
        last = ""
        for _attempt in range(1, self.attempts + 1):
            proc = run(
                ["timeout", f"{self.timeout}s", self.hdc_bin, "-t", device, "file", "send", str(local), remote],
                timeout=self.timeout + 10,
            )
            last = proc.stdout
            if hdc_ok(proc.returncode, proc.stdout):
                return proc.stdout
            time.sleep(1)
        raise RuntimeError(last)


def remote_env(
    prefix: str,
    bridge_library: str,
    domain_id: int,
    *,
    broker_log: str,
    graph_debug: bool,
) -> str:
    values = {
        "LD_LIBRARY_PATH": f"{prefix}/lib:/data/local/tmp/ohos-prefix/lib:/data/local/tmp/ohos-fastdds/lib:/data/local/release/usr/lib:/system/lib64/platformsdk:/system/lib64/chipset-pub-sdk:/system/lib64",
        "LD_PRELOAD": f"{prefix}/lib/librmw_implementation.so",
        "HOME": "/data/local/tmp",
        "ROS_LOG_DIR": "/data/local/tmp/roslogs",
        "PYTHONHOME": "/data/local/release/usr",
        "AMENT_PREFIX_PATH": f"{prefix}:/data/local/tmp/ohos-prefix",
        "CMAKE_PREFIX_PATH": f"{prefix}:/data/local/tmp/ohos-prefix:/data/local/tmp/ohos-fastdds",
        "COLCON_PREFIX_PATH": f"{prefix}:/data/local/tmp/ohos-prefix",
        "PYTHONPATH": f"{prefix}/lib/python3.12/site-packages:/data/local/tmp/ohos-prefix/lib/python3.12/site-packages:/data/local/tmp/ohos-prefix/lib/python3.11/site-packages",
        "ROS_DOMAIN_ID": str(domain_id),
        "RMW_IMPLEMENTATION": "rmw_mdds_cpp",
        "RMW_MDDS_BROKER": "1",
        "RMW_MDDS_BRIDGE_LIBRARY": bridge_library,
        "RMW_MDDS_BROKER_LOG": broker_log,
        "RMW_MDDS_BROKER_PID_FILE": f"{str(Path(broker_log).parent)}/broker.pid",
    }
    if graph_debug:
        values["RMW_MDDS_GRAPH_DEBUG"] = "1"
    return "; ".join(f"export {key}={shlex.quote(value)}" for key, value in values.items()) + ";"


def count_pattern(text: str, pattern: str) -> int:
    return sum(1 for line in text.splitlines() if pattern in line)


def parse_int_from_shell(output: str) -> int:
    for token in reversed(output.replace("\r", "\n").split()):
        if token.isdigit():
            return int(token)
    return 0


def read_all_logs(hdc: Hdc, device: str, workdir: str, glob: str) -> str:
    command = (
        f"grep -h -E '^(CLIENT_|MANY_CLIENTS_|ONE_CLIENT_)' "
        f"{shlex.quote(workdir)}/{glob} 2>/dev/null || true"
    )
    return hdc.shell(device, command)


def remote_count_pattern(hdc: Hdc, device: str, workdir: str, glob: str, pattern: str) -> int:
    command = (
        f"grep -h -o {shlex.quote(pattern)} "
        f"{shlex.quote(workdir)}/{glob} 2>/dev/null | wc -l"
    )
    return parse_int_from_shell(hdc.shell(device, command))


def remote_cleanup_command(workdir: str) -> str:
    quoted_workdir = shlex.quote(workdir)
    parts = [
        "for pid in $(ps -ef | grep -E 'rmw_mdds_service_stress.py|rmw_mdds_broker|python3.12' | "
        "grep -v grep | sed -E 's/^ *[^ ]+ +([0-9]+).*/\\1/'); do "
        "kill -9 \"$pid\" 2>/dev/null; "
        "done",
        "for p in /proc/[0-9]*; do "
        '[ -r "$p/status" ] || continue; '
        'pid=${p#/proc/}; '
        'name=$(sed -n "s/^Name:[[:space:]]*//p" "$p/status" 2>/dev/null); '
        'if [ "$name" = "rmw_mdds_broker" ]; then kill -9 "$pid" 2>/dev/null; continue; fi; '
        'if [ -r "$p/cmdline" ] && grep -q "rmw_mdds_service_stress.py" "$p/cmdline" 2>/dev/null; '
        'then kill -9 "$pid" 2>/dev/null; fi; '
        "done",
        "for _i in 1 2 3 4 5; do "
        "grep -q '/data/local/tmp/rmw_mdds_cpp.sock' /proc/net/unix 2>/dev/null || break; "
        "sleep 1; "
        "done",
        "rm -f /data/local/tmp/rmw_mdds_cpp.sock "
        "/data/local/tmp/rmw_mdds_cpp.sock.autostart.lock "
        "/data/local/tmp/rmw_mdds_cpp.sock.listener.lock",
        "rm -rf /data/local/tmp/rmw_mdds_cpp.sock.autostart.lockdir "
        "/data/local/tmp/rmw_mdds_cpp.sock.listener.lockdir",
        f"mkdir -p {quoted_workdir} /data/local/tmp/roslogs",
        "echo STRESS_CLEANUP_DONE",
    ]
    return "; ".join(parts)


def remote_stop_stress_command() -> str:
    parts = [
        "for pid in $(ps -ef | grep -E 'rmw_mdds_service_stress.py|rmw_mdds_broker' | "
        "grep -v grep | sed -E 's/^ *[^ ]+ +([0-9]+).*/\\1/'); do "
        "kill -9 \"$pid\" 2>/dev/null; "
        "done",
        "for p in /proc/[0-9]*; do "
        '[ -r "$p/status" ] || continue; '
        'pid=${p#/proc/}; '
        'name=$(sed -n "s/^Name:[[:space:]]*//p" "$p/status" 2>/dev/null); '
        'if [ "$name" = "rmw_mdds_broker" ]; then kill -9 "$pid" 2>/dev/null; continue; fi; '
        'if [ -r "$p/cmdline" ] && grep -q "rmw_mdds_service_stress.py" "$p/cmdline" 2>/dev/null; '
        'then kill -9 "$pid" 2>/dev/null; fi; '
        "done",
        "rm -f /data/local/tmp/rmw_mdds_cpp.sock "
        "/data/local/tmp/rmw_mdds_cpp.sock.autostart.lock "
        "/data/local/tmp/rmw_mdds_cpp.sock.listener.lock",
        "rm -rf /data/local/tmp/rmw_mdds_cpp.sock.autostart.lockdir "
        "/data/local/tmp/rmw_mdds_cpp.sock.listener.lockdir",
        "echo STRESS_STOP_DONE",
    ]
    return "; ".join(parts)


def remote_stress_process_count_command() -> str:
    return (
        "ps -ef | grep 'rmw_mdds_service_stress.py' | "
        "grep -v grep | wc -l"
    )


def remote_broker_ready_command() -> str:
    return (
        "if grep -q '/data/local/tmp/rmw_mdds_cpp.sock' /proc/net/unix 2>/dev/null; "
        "then echo BROKER_READY; else echo BROKER_NOT_READY; fi"
    )


def wait_for_broker_ready(hdc: Hdc, device: str, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        output = hdc.shell(device, remote_broker_ready_command())
        if "BROKER_READY" in output:
            return True
        time.sleep(0.2)
    return False


def prestart_broker(args: argparse.Namespace, hdc: Hdc, device: str, env: str, workdir: str) -> None:
    broker = f"{args.prefix}/lib/rmw_mdds_cpp/rmw_mdds_broker"
    hdc.shell(
        device,
        f"nohup sh -c {shlex.quote(f'{env} {broker} --socket /data/local/tmp/rmw_mdds_cpp.sock > {workdir}/broker.log 2>&1')} >/dev/null 2>&1 & echo BROKER_PRESTARTED",
    )
    if not wait_for_broker_ready(hdc, device, args.prestart_broker_timeout):
        raise RuntimeError(f"broker did not become ready on {device}")


def make_summary(
    *,
    mode: str,
    round_id: int,
    clients: int,
    domain_id: int,
    workdir: str,
    client_logs: str,
    server_log: str,
    process_timeout: int,
    elapsed_ms: int,
) -> dict[str, object]:
    total = clients
    if mode == "one_client_many_requests":
        client_created = count_pattern(client_logs, "CLIENT_CREATED")
        client_started = count_pattern(client_logs, "ONE_CLIENT_MANY_STARTED")
        created_expected = 1
        destroy_expected = 1
    elif mode == "many_clients_one_process":
        client_created = count_pattern(client_logs, "CLIENT_CREATED")
        client_started = count_pattern(client_logs, "MANY_CLIENTS_STARTED")
        created_expected = clients
        destroy_expected = 1
    else:
        client_created = count_pattern(client_logs, "CLIENT_CREATED")
        client_started = count_pattern(client_logs, "CLIENT_STARTED")
        created_expected = clients
        destroy_expected = clients

    summary = {
        "MODE": mode,
        "ROUND": round_id,
        "TOTAL": total,
        "ROS_DOMAIN_ID": str(domain_id),
        "RMW": "rmw_mdds_cpp",
        "WORKDIR": workdir,
        "CLIENT_STARTED": client_started,
        "CLIENT_CREATED": client_created,
        "CLIENT_WAIT_OK": count_pattern(client_logs, "CLIENT_WAIT_OK"),
        "CLIENT_SENT": count_pattern(client_logs, "CLIENT_SENT"),
        "CLIENT_OK": count_pattern(client_logs, "CLIENT_OK"),
        "CLIENT_TIMEOUT": count_pattern(client_logs, "CLIENT_TIMEOUT"),
        "CLIENT_ERROR": count_pattern(client_logs, "CLIENT_ERROR"),
        "CLIENT_DESTROY_START": count_pattern(client_logs, "CLIENT_DESTROY_START"),
        "CLIENT_DESTROY_DONE": count_pattern(client_logs, "CLIENT_DESTROY_DONE"),
        "CLIENT_SHUTDOWN_START": count_pattern(client_logs, "CLIENT_SHUTDOWN_START"),
        "CLIENT_SHUTDOWN_DONE": count_pattern(client_logs, "CLIENT_SHUTDOWN_DONE"),
        "DESTROY_EXPECTED": destroy_expected,
        "SERVER_REQ": count_pattern(server_log, "SERVER_REQ"),
        "PROCESS_TIMEOUT": process_timeout,
        "PROCESS_ERROR": 0,
        "ELAPSED_MS": elapsed_ms,
    }
    summary["PASS"] = (
        summary["CLIENT_CREATED"] == created_expected
        and summary["CLIENT_SENT"] == clients
        and summary["CLIENT_OK"] == clients
        and summary["CLIENT_TIMEOUT"] == 0
        and summary["CLIENT_ERROR"] == 0
        and summary["SERVER_REQ"] == clients
        and summary["PROCESS_TIMEOUT"] == 0
        and summary["CLIENT_DESTROY_DONE"] == destroy_expected
        and summary["CLIENT_SHUTDOWN_DONE"] == destroy_expected
    )
    return summary


def make_remote_summary(
    *,
    hdc: Hdc,
    client_device: str,
    server_device: str,
    mode: str,
    round_id: int,
    clients: int,
    domain_id: int,
    workdir: str,
    process_timeout: int,
    elapsed_ms: int,
) -> dict[str, object]:
    if mode == "one_client_many_requests":
        client_started = remote_count_pattern(hdc, client_device, workdir, "client*.log", "ONE_CLIENT_MANY_STARTED")
        created_expected = 1
        destroy_expected = 1
    elif mode == "many_clients_one_process":
        client_started = remote_count_pattern(hdc, client_device, workdir, "client*.log", "MANY_CLIENTS_STARTED")
        created_expected = clients
        destroy_expected = 1
    else:
        client_started = remote_count_pattern(hdc, client_device, workdir, "client*.log", "CLIENT_STARTED")
        created_expected = clients
        destroy_expected = clients

    summary = {
        "MODE": mode,
        "ROUND": round_id,
        "TOTAL": clients,
        "ROS_DOMAIN_ID": str(domain_id),
        "RMW": "rmw_mdds_cpp",
        "WORKDIR": workdir,
        "CLIENT_STARTED": client_started,
        "CLIENT_CREATED": remote_count_pattern(hdc, client_device, workdir, "client*.log", "CLIENT_CREATED"),
        "CLIENT_WAIT_OK": remote_count_pattern(hdc, client_device, workdir, "client*.log", "CLIENT_WAIT_OK"),
        "CLIENT_SENT": remote_count_pattern(hdc, client_device, workdir, "client*.log", "CLIENT_SENT"),
        "CLIENT_OK": remote_count_pattern(hdc, client_device, workdir, "client*.log", "CLIENT_OK"),
        "CLIENT_TIMEOUT": remote_count_pattern(hdc, client_device, workdir, "client*.log", "CLIENT_TIMEOUT"),
        "CLIENT_ERROR": remote_count_pattern(hdc, client_device, workdir, "client*.log", "CLIENT_ERROR"),
        "CLIENT_DESTROY_START": remote_count_pattern(hdc, client_device, workdir, "client*.log", "CLIENT_DESTROY_START"),
        "CLIENT_DESTROY_DONE": remote_count_pattern(hdc, client_device, workdir, "client*.log", "CLIENT_DESTROY_DONE"),
        "CLIENT_SHUTDOWN_START": remote_count_pattern(hdc, client_device, workdir, "client*.log", "CLIENT_SHUTDOWN_START"),
        "CLIENT_SHUTDOWN_DONE": remote_count_pattern(hdc, client_device, workdir, "client*.log", "CLIENT_SHUTDOWN_DONE"),
        "DESTROY_EXPECTED": destroy_expected,
        "SERVER_REQ": remote_count_pattern(hdc, server_device, workdir, "server.log", "SERVER_REQ"),
        "PROCESS_TIMEOUT": process_timeout,
        "PROCESS_ERROR": 0,
        "ELAPSED_MS": elapsed_ms,
    }
    summary["PASS"] = (
        summary["CLIENT_CREATED"] == created_expected
        and summary["CLIENT_SENT"] == clients
        and summary["CLIENT_OK"] == clients
        and summary["CLIENT_TIMEOUT"] == 0
        and summary["CLIENT_ERROR"] == 0
        and summary["SERVER_REQ"] == clients
        and summary["PROCESS_TIMEOUT"] == 0
        and summary["CLIENT_DESTROY_DONE"] == destroy_expected
        and summary["CLIENT_SHUTDOWN_DONE"] == destroy_expected
    )
    return summary


def launch_round(args: argparse.Namespace, hdc: Hdc, helper_remote: str, round_id: int) -> dict[str, object]:
    domain_id = args.domain + (round_id - 1 if args.domain_stride else 0)
    service_name = f"/rmw_mdds_stress_{args.mode}_{args.clients}_r{round_id}_{int(time.time())}"
    workdir = f"{args.log_root}/{args.mode}_n{args.clients}_r{round_id}_{int(time.time())}"
    env = remote_env(
        args.prefix,
        args.bridge_library,
        domain_id,
        broker_log=f"{workdir}/broker.log",
        graph_debug=args.graph_debug,
    )
    python = "/data/local/release/usr/bin/python3.12"
    for device in (args.server_device, args.client_device):
        hdc.shell(device, remote_cleanup_command(workdir))
    if args.prestart_broker:
        for device in (args.server_device, args.client_device):
            prestart_broker(args, hdc, device, env, workdir)

    server_timeout = args.server_timeout
    client_timeout = args.client_timeout
    hdc.shell(
        args.server_device,
        f"nohup sh -c {shlex.quote(f'{env} export STRESS_SERVER_DRAIN_SEC={args.server_drain}; {python} {helper_remote} server {service_name} {args.clients} {server_timeout} > {workdir}/server.log 2>&1')} >/dev/null 2>&1 & echo SERVER_STARTED",
    )
    time.sleep(args.warmup)

    start = time.monotonic()
    if args.mode == "processes":
        launch = (
            f"{env} "
            f"for i in $(seq 1 {args.clients}); do "
            f"STRESS_CLIENT_ID=$i {python} {helper_remote} client_once "
            f"{shlex.quote(service_name)} 1 {client_timeout} "
            f"> {shlex.quote(workdir)}/client_${{i}}.log 2>&1 & "
            f"done; echo CLIENTS_STARTED"
        )
        hdc.shell(args.client_device, launch)
    else:
        client_mode = args.mode
        hdc.shell(
            args.client_device,
            f"nohup sh -c {shlex.quote(f'{env} {python} {helper_remote} {client_mode} {service_name} {args.clients} {client_timeout} > {workdir}/client_1.log 2>&1')} >/dev/null 2>&1 & echo CLIENT_STARTED",
        )

    deadline = time.monotonic() + args.round_timeout
    while time.monotonic() < deadline:
        client_done = parse_int_from_shell(
            hdc.shell(
                args.client_device,
                f"grep -h 'CLIENT_OK' {workdir}/client*.log 2>/dev/null | wc -l",
            )
        )
        server_done = parse_int_from_shell(
            hdc.shell(
                args.server_device,
                f"grep -h 'SERVER_REQ' {workdir}/server.log 2>/dev/null | wc -l",
            )
        )
        if client_done >= args.clients and server_done >= args.clients:
            break
        time.sleep(args.poll_interval)

    if args.client_grace > 0:
        time.sleep(args.client_grace)

    elapsed_ms = int((time.monotonic() - start) * 1000)
    process_timeout = parse_int_from_shell(
        hdc.shell(args.client_device, remote_stress_process_count_command())
    )
    summary = make_remote_summary(
        hdc=hdc,
        client_device=args.client_device,
        server_device=args.server_device,
        mode=args.mode,
        round_id=round_id,
        clients=args.clients,
        domain_id=domain_id,
        workdir=workdir,
        process_timeout=process_timeout,
        elapsed_ms=elapsed_ms,
    )
    # If board-side stdio is still settling, re-count on the board before
    # producing an artifact. Counts are intentionally computed on-device so a
    # partial HDC stdout read cannot remove individual log lines.
    for _attempt in range(args.log_settle_attempts):
        if summary["PASS"] or summary["PROCESS_TIMEOUT"] != 0:
            break
        time.sleep(1)
        summary = make_remote_summary(
            hdc=hdc,
            client_device=args.client_device,
            server_device=args.server_device,
            mode=args.mode,
            round_id=round_id,
            clients=args.clients,
            domain_id=domain_id,
            workdir=workdir,
            process_timeout=process_timeout,
            elapsed_ms=elapsed_ms,
        )

    with tempfile.NamedTemporaryFile("w", delete=False, suffix=".json") as tmp:
        json.dump(summary, tmp, indent=2, sort_keys=True)
        tmp.write("\n")
        tmp_path = Path(tmp.name)
    try:
        hdc.send(args.client_device, tmp_path, f"{workdir}/summary.json")
    finally:
        tmp_path.unlink(missing_ok=True)
        for device in (args.server_device, args.client_device):
            try:
                hdc.shell(device, remote_stop_stress_command())
            except RuntimeError as exc:
                print(f"warning: failed to stop stress processes on {device}: {exc}", file=sys.stderr)

    return summary


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("server_device")
    parser.add_argument("client_device")
    parser.add_argument("--domain", type=int, default=int(os.environ.get("ROS_DOMAIN_ID", "460")))
    parser.add_argument("--domain-stride", action="store_true", help="increment ROS_DOMAIN_ID for each round")
    parser.add_argument("--mode", choices=("processes", "many_clients_one_process", "one_client_many_requests"), default="processes")
    parser.add_argument("--clients", type=int, default=50)
    parser.add_argument("--rounds", type=int, default=1)
    parser.add_argument("--warmup", type=float, default=8.0)
    parser.add_argument("--client-timeout", type=float, default=75.0)
    parser.add_argument("--server-timeout", type=float, default=120.0)
    parser.add_argument("--server-drain", type=float, default=3.0, help="seconds to spin the server after it observes all expected requests")
    parser.add_argument("--round-timeout", type=float, default=90.0)
    parser.add_argument("--client-grace", type=float, default=5.0)
    parser.add_argument("--log-settle-attempts", type=int, default=10)
    parser.add_argument("--prestart-broker", action="store_true")
    parser.add_argument("--prestart-broker-timeout", type=float, default=10.0)
    parser.add_argument("--poll-interval", type=float, default=2.0)
    parser.add_argument("--log-root", default="/data/local/tmp/rmw_mdds_service_stress_current")
    parser.add_argument("--prefix", default="/data/local/tmp/ohos-colcon-rk3588a")
    parser.add_argument("--bridge-library", default="/data/local/tmp/ohos-colcon-rk3588a/lib/libmdds_bridge_shared.z.so")
    parser.add_argument("--graph-debug", action="store_true", help="enable RMW_MDDS_GRAPH_DEBUG and per-round broker.log")
    parser.add_argument("--hdc-bin", default=os.environ.get("HDC_BIN", "hdc"))
    parser.add_argument("--hdc-timeout", type=int, default=180)
    parser.add_argument("--hdc-attempts", type=int, default=3)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.clients <= 0 or args.rounds <= 0:
        print("clients and rounds must be positive", file=sys.stderr)
        return 2

    hdc = Hdc(args.hdc_bin, args.hdc_timeout, args.hdc_attempts)
    helper_remote = "/data/local/tmp/rmw_mdds_service_stress.py"
    with tempfile.NamedTemporaryFile("w", delete=False, suffix=".py") as helper:
        helper.write(HELPER)
        helper_path = Path(helper.name)
    try:
        for device in (args.server_device, args.client_device):
            hdc.send(device, helper_path, helper_remote)
            hdc.shell(device, f"chmod 755 {helper_remote} && echo HELPER_READY")
    finally:
        helper_path.unlink(missing_ok=True)

    summaries = []
    for round_id in range(1, args.rounds + 1):
        summary = launch_round(args, hdc, helper_remote, round_id)
        summaries.append(summary)
        print(
            "SERVICE_STRESS|{status}|mode={mode}|round={round}|clients={clients}|"
            "CLIENT_CREATED={created}|CLIENT_SENT={sent}|CLIENT_OK={ok}|"
            "CLIENT_TIMEOUT={timeout}|CLIENT_ERROR={error}|SERVER_REQ={server}|"
            "PROCESS_TIMEOUT={proc}|ELAPSED_MS={elapsed}|WORKDIR={workdir}".format(
                status="PASS" if summary["PASS"] else "FAIL",
                mode=summary["MODE"],
                round=summary["ROUND"],
                clients=summary["TOTAL"],
                created=summary["CLIENT_CREATED"],
                sent=summary["CLIENT_SENT"],
                ok=summary["CLIENT_OK"],
                timeout=summary["CLIENT_TIMEOUT"],
                error=summary["CLIENT_ERROR"],
                server=summary["SERVER_REQ"],
                proc=summary["PROCESS_TIMEOUT"],
                elapsed=summary["ELAPSED_MS"],
                workdir=summary["WORKDIR"],
            ),
            flush=True,
        )
    passed = sum(1 for item in summaries if item["PASS"])
    print(f"SERVICE_STRESS_SUMMARY|pass={passed}|fail={len(summaries) - passed}|rounds={len(summaries)}")
    return 0 if passed == len(summaries) else 1


if __name__ == "__main__":
    raise SystemExit(main())
