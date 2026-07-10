#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage: run_rmw_mdds_board_graph_churn.sh <device-id> [domain-id] [rounds]

Runs an RK3588A board-side rmw_mdds graph churn gate. The default cli mode
starts a unique demo_nodes_cpp talker and AddTwoInts service server each round,
waits until ros2 node/topic/service graph commands see the node, topic, and
service through rmw_mdds_cpp, kills both nodes, then waits until their graph
entries disappear. The rclpy mode creates and destroys the same graph entity
classes in-process so longer RMW graph churn gates can finish in practical time.
The rclpy_action mode creates and destroys action client/server entities and
validates the action-specific graph APIs.
The host result is based on the board-side PASS marker, not the hdc exit code.

Optional environment:
  RMW_MDDS_GRAPH_CHURN_MODE=cli|rclpy|rclpy_action
                                             graph-churn implementation mode
  RMW_MDDS_GRAPH_CHURN_CLI_TIMEOUT=<seconds>  per-query ros2 CLI timeout
  RMW_MDDS_GRAPH_CHURN_OBSERVE_TIMEOUT=<seconds>
                                               rclpy per-phase graph wait timeout
  RMW_MDDS_GRAPH_CHURN_MIN_SECONDS=<seconds>   rclpy/rclpy_action minimum
                                               board-side elapsed time before PASS
  RMW_MDDS_GRAPH_CHURN_PROGRESS_INTERVAL=<n>    rclpy/rclpy_action round log
                                               interval, default: 1
  RMW_MDDS_GRAPH_CHURN_TIMING=1                print rclpy per-round timing
  RMW_MDDS_GRAPH_DEBUG=1                       forward rmw_mdds graph debug logs
EOF
}

if [[ $# -lt 1 || $# -gt 3 ]]; then
  usage
  exit 2
fi

case "$1" in
  -h|--help)
    usage
    exit 0
    ;;
esac

DEVICE="$1"
DOMAIN_ID="${2:-93}"
ROUNDS="${3:-100}"
MODE="${RMW_MDDS_GRAPH_CHURN_MODE:-cli}"
CLI_TIMEOUT="${RMW_MDDS_GRAPH_CHURN_CLI_TIMEOUT:-8}"
OBSERVE_TIMEOUT="${RMW_MDDS_GRAPH_CHURN_OBSERVE_TIMEOUT:-5.0}"
MIN_SECONDS="${RMW_MDDS_GRAPH_CHURN_MIN_SECONDS:-0}"
PROGRESS_INTERVAL="${RMW_MDDS_GRAPH_CHURN_PROGRESS_INTERVAL:-1}"
TIMING="${RMW_MDDS_GRAPH_CHURN_TIMING:-0}"
GRAPH_DEBUG="${RMW_MDDS_GRAPH_DEBUG:-0}"

if ! [[ "${ROUNDS}" =~ ^[0-9]+$ ]] || [[ "${ROUNDS}" -le 0 ]]; then
  echo "rounds must be a positive integer" >&2
  exit 2
fi
case "${MODE}" in
  cli|rclpy|rclpy_action)
    ;;
  *)
    echo "RMW_MDDS_GRAPH_CHURN_MODE must be cli, rclpy, or rclpy_action" >&2
    exit 2
    ;;
esac
if ! [[ "${CLI_TIMEOUT}" =~ ^[0-9]+$ ]] || [[ "${CLI_TIMEOUT}" -le 0 ]]; then
  echo "RMW_MDDS_GRAPH_CHURN_CLI_TIMEOUT must be a positive integer" >&2
  exit 2
fi
if ! [[ "${OBSERVE_TIMEOUT}" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
  echo "RMW_MDDS_GRAPH_CHURN_OBSERVE_TIMEOUT must be numeric" >&2
  exit 2
fi
if ! [[ "${MIN_SECONDS}" =~ ^[0-9]+$ ]]; then
  echo "RMW_MDDS_GRAPH_CHURN_MIN_SECONDS must be a non-negative integer" >&2
  exit 2
fi
if ! [[ "${PROGRESS_INTERVAL}" =~ ^[0-9]+$ ]] || [[ "${PROGRESS_INTERVAL}" -le 0 ]]; then
  echo "RMW_MDDS_GRAPH_CHURN_PROGRESS_INTERVAL must be a positive integer" >&2
  exit 2
fi
case "${TIMING}" in
  0|1)
    ;;
  *)
    echo "RMW_MDDS_GRAPH_CHURN_TIMING must be 0 or 1" >&2
    exit 2
    ;;
esac
case "${GRAPH_DEBUG}" in
  0|1)
    ;;
  *)
    echo "RMW_MDDS_GRAPH_DEBUG must be 0 or 1" >&2
    exit 2
    ;;
esac

HDC="${HDC_BIN:-hdc}"
REMOTE_SCRIPT="/data/local/tmp/rmw_mdds_board_graph_churn.sh"
REMOTE_LOG="/data/local/tmp/rmw_mdds_board_graph_churn"

TMP_SCRIPT="$(mktemp "${TMPDIR:-/tmp}/rmw_mdds_board_graph_churn.XXXXXX.sh")"
TMP_OUTPUT="$(mktemp "${TMPDIR:-/tmp}/rmw_mdds_board_graph_churn.XXXXXX.log")"
cleanup() {
  rm -f "${TMP_SCRIPT}" "${TMP_OUTPUT}"
}
trap cleanup EXIT

cat >"${TMP_SCRIPT}" <<'REMOTE_EOF'
#!/system/bin/sh
set -u

DOMAIN_ID="${1:-93}"
ROUNDS="${2:-100}"
PFX="${ROS2_OHOS_REMOTE_PREFIX:-/data/local/tmp/ohos-colcon-rk3588a}"
LOG="/data/local/tmp/rmw_mdds_board_graph_churn"
BR="${RMW_MDDS_BRIDGE_LIBRARY:-${PFX}/lib/libmdds_bridge_shared.z.so}"
MODE="${RMW_MDDS_GRAPH_CHURN_MODE:-cli}"
CLI_TIMEOUT="${RMW_MDDS_GRAPH_CHURN_CLI_TIMEOUT:-8}"
OBSERVE_TIMEOUT="${RMW_MDDS_GRAPH_CHURN_OBSERVE_TIMEOUT:-5.0}"
TIMING="${RMW_MDDS_GRAPH_CHURN_TIMING:-0}"
LDP="${PFX}/lib:/data/local/tmp/ohos-prefix/lib:/data/local/tmp/ohos-fastdds/lib:/data/local/release/usr/lib:/system/lib64/platformsdk:/system/lib64/chipset-pub-sdk:/system/lib64"

if [ -f /data/local/tmp/rmw_mdds_env.sh ]; then
  . /data/local/tmp/rmw_mdds_env.sh
fi

export HOME=/data/local/tmp
export ROS_LOG_DIR="${LOG}"
export LD_LIBRARY_PATH="${LDP}"
export RMW_IMPLEMENTATION=rmw_mdds_cpp
export RMW_MDDS_BROKER=1
export RMW_MDDS_BRIDGE_LIBRARY="${BR}"
export ROS_DOMAIN_ID="${DOMAIN_ID}"

cleanup_processes() {
  ps -ef | grep -E 'mdds_graph_churn|ros2 node list|ros2 topic list|ros2 service list' | grep -v grep |
    while read -r _user pid _rest; do
      kill -9 "${pid}" 2>/dev/null || true
    done
}

run_rclpy_graph_churn() {
  py="${RMW_MDDS_GRAPH_CHURN_PYTHON:-/data/local/release/usr/bin/python3.12}"
  export PYTHONHOME="${PYTHONHOME:-/data/local/release/usr}"
  export PYTHONPATH="${PFX}/lib/python3.12/site-packages:/data/local/tmp/ohos-prefix/lib/python3.12/site-packages:/data/local/tmp/ohos-prefix/lib/python3.11/site-packages:${PYTHONPATH:-}"

  rm -rf "${LOG}"
  mkdir -p "${LOG}"
  cleanup_processes

  echo "GRAPH_CHURN_START domain=${ROS_DOMAIN_ID} rounds=${ROUNDS} rmw=${RMW_IMPLEMENTATION} mode=rclpy"

  "${py}" - "${ROUNDS}" "${LOG}" <<'PY_EOF'
import gc
import os
import sys
import time

rounds = int(sys.argv[1])
log_dir = sys.argv[2]
observe_timeout = float(os.environ.get("RMW_MDDS_GRAPH_CHURN_OBSERVE_TIMEOUT", "5.0"))
min_seconds = float(os.environ.get("RMW_MDDS_GRAPH_CHURN_MIN_SECONDS", "0"))
progress_interval = int(os.environ.get("RMW_MDDS_GRAPH_CHURN_PROGRESS_INTERVAL", "1"))
spin_timeout = float(os.environ.get("RMW_MDDS_GRAPH_CHURN_SPIN_TIMEOUT", "0.02"))
poll_sleep = float(os.environ.get("RMW_MDDS_GRAPH_CHURN_POLL_SLEEP", "0.01"))
emit_timing = os.environ.get("RMW_MDDS_GRAPH_CHURN_TIMING", "0") == "1"

import rclpy
from example_interfaces.srv import AddTwoInts
from rclpy.node import Node
from std_msgs.msg import String


def fq_node_names(node):
    names = set()
    for name, namespace in node.get_node_names_and_namespaces():
        namespace = namespace or "/"
        if namespace == "/":
            names.add("/" + name)
        else:
            names.add(namespace.rstrip("/") + "/" + name)
        names.add(name)
    return names


def named_entities(node):
    topics = {name for name, _types in node.get_topic_names_and_types()}
    services = {name for name, _types in node.get_service_names_and_types()}
    return topics, services


def spin_until(observer, predicate, timeout_sec):
    deadline = time.monotonic() + timeout_sec
    last = None
    while time.monotonic() < deadline:
        rclpy.spin_once(observer, timeout_sec=spin_timeout)
        names = fq_node_names(observer)
        topics, services = named_entities(observer)
        last = (names, topics, services)
        if predicate(names, topics, services):
            return True, last
        time.sleep(poll_sleep)
    return False, last


def service_callback(request, response):
    response.sum = request.a + request.b
    return response


def should_log_round(index, target_rounds, elapsed, min_elapsed):
    return (
        index == 1
        or index == target_rounds
        or index % progress_interval == 0
        or (min_elapsed > 0 and elapsed >= min_elapsed)
    )


rclpy.init(args=None)
rmw = rclpy.get_rmw_implementation_identifier()
pass_count = 0
fail_count = 0
created_topics = 0
created_services = 0
destroyed_topics = 0
destroyed_services = 0
first_failure = ""
started = time.monotonic()

try:
    for i in range(1, rounds + 1):
        round_start = time.monotonic()
        observer = Node(f"mdds_graph_churn_fast_observer_{i}")
        node_name = f"mdds_graph_churn_fast_{i}"
        topic = f"/mdds_graph_churn_fast_topic_{i}"
        service = f"/mdds_graph_churn_fast_srv_{i}"
        node = Node(node_name)
        publisher = node.create_publisher(String, topic, 10)
        srv = node.create_service(AddTwoInts, service, service_callback)
        rclpy.spin_once(node, timeout_sec=0.0)

        found, before = spin_until(
            observer,
            lambda names, topics, services: (
                node_name in names
                or ("/" + node_name) in names
            )
            and topic in topics
            and service in services,
            observe_timeout,
        )
        found_done = time.monotonic()
        if found:
            created_topics += 1
            created_services += 1

        node.destroy_service(srv)
        srv = None
        node.destroy_publisher(publisher)
        publisher = None
        node.destroy_node()
        node = None
        observer.destroy_node()
        observer = None
        gc.collect()
        destroy_done = time.monotonic()

        observer_after = Node(f"mdds_graph_churn_fast_observer_after_{i}")
        gone, after = spin_until(
            observer_after,
            lambda names, topics, services: (
                node_name not in names
                and ("/" + node_name) not in names
                and topic not in topics
                and service not in services
            ),
            observe_timeout,
        )
        observer_after.destroy_node()
        observer_after = None
        gc.collect()
        gone_done = time.monotonic()
        if gone:
            destroyed_topics += 1
            destroyed_services += 1

        if emit_timing:
            print(
                "ROUND_TIMING|"
                f"{i}|found_wait={found_done - round_start:.3f}|"
                f"destroy_wait={destroy_done - found_done:.3f}|"
                f"gone_wait={gone_done - destroy_done:.3f}|"
                f"total={gone_done - round_start:.3f}",
                flush=True,
            )

        elapsed_now = time.monotonic() - started
        if found and gone:
            pass_count += 1
            if should_log_round(i, rounds, elapsed_now, min_seconds):
                print(
                    f"ROUND|{i}|PASS|node=/{node_name}|topic={topic}|service={service}|"
                    f"elapsed_sec={elapsed_now:.3f}",
                    flush=True,
                )
        else:
            fail_count += 1
            if not first_failure:
                first_failure = (
                    f"round={i} found={int(found)} gone={int(gone)} "
                    f"before={before!r} after={after!r}"
                )
            print(
                f"ROUND|{i}|FAIL|found={int(found)}|gone={int(gone)}|"
                f"node=/{node_name}|topic={topic}|service={service}",
                flush=True,
            )
            break
        if min_seconds > 0 and elapsed_now >= min_seconds:
            break
finally:
    rclpy.shutdown()

elapsed_sec = time.monotonic() - started
duration_met = min_seconds <= 0 or elapsed_sec >= min_seconds
summary_path = os.path.join(log_dir, "summary.txt")
with open(summary_path, "w", encoding="utf-8") as summary:
    summary.write(f"GRAPH_CHURN_MODE=rclpy\n")
    summary.write(f"GRAPH_CHURN_TARGET_ROUNDS={rounds}\n")
    summary.write(f"GRAPH_CHURN_ROUNDS={pass_count}\n")
    summary.write(f"GRAPH_CHURN_PASS={pass_count}\n")
    summary.write(f"GRAPH_CHURN_FAIL={fail_count}\n")
    summary.write(f"GRAPH_CHURN_TOPICS_CREATED={created_topics}\n")
    summary.write(f"GRAPH_CHURN_SERVICES_CREATED={created_services}\n")
    summary.write(f"GRAPH_CHURN_TOPICS_DESTROYED={destroyed_topics}\n")
    summary.write(f"GRAPH_CHURN_SERVICES_DESTROYED={destroyed_services}\n")
    summary.write(f"GRAPH_CHURN_MIN_SECONDS={min_seconds:.3f}\n")
    summary.write(f"GRAPH_CHURN_ELAPSED_SEC={elapsed_sec:.3f}\n")
    summary.write(f"GRAPH_CHURN_DURATION_MET={int(duration_met)}\n")
    summary.write(f"GRAPH_CHURN_PROGRESS_INTERVAL={progress_interval}\n")
    summary.write(f"RMW_IMPLEMENTATION={rmw}\n")
    summary.write(f"ROS_DOMAIN_ID={os.environ.get('ROS_DOMAIN_ID', '')}\n")
    if first_failure:
        summary.write(f"FIRST_FAILURE={first_failure}\n")

with open(summary_path, "r", encoding="utf-8") as summary:
    print(summary.read(), end="", flush=True)

if fail_count == 0 and duration_met:
    print(
        "RESULT|rmw_mdds_board_graph_churn_fast|PASS|"
        f"rounds={pass_count}|topics_created={created_topics}|"
        f"services_created={created_services}|topics_destroyed={destroyed_topics}|"
        f"services_destroyed={destroyed_services}|elapsed_sec={elapsed_sec:.3f}",
        flush=True,
    )
    print("rmw_mdds_board_graph_churn_fast_ok", flush=True)
    sys.exit(0)

print(
    "RESULT|rmw_mdds_board_graph_churn_fast|FAIL|"
    f"rounds={pass_count}|target_rounds={rounds}|pass={pass_count}|fail={fail_count}|"
    f"topics_created={created_topics}|services_created={created_services}|"
    f"topics_destroyed={destroyed_topics}|services_destroyed={destroyed_services}|"
    f"elapsed_sec={elapsed_sec:.3f}|duration_met={int(duration_met)}",
    flush=True,
)
sys.exit(1)
PY_EOF
}

run_rclpy_action_graph_churn() {
  py="${RMW_MDDS_GRAPH_CHURN_PYTHON:-/data/local/release/usr/bin/python3.12}"
  export PYTHONHOME="${PYTHONHOME:-/data/local/release/usr}"
  export PYTHONPATH="${PFX}/lib/python3.12/site-packages:/data/local/tmp/ohos-prefix/lib/python3.12/site-packages:/data/local/tmp/ohos-prefix/lib/python3.11/site-packages:${PYTHONPATH:-}"

  rm -rf "${LOG}"
  mkdir -p "${LOG}"
  cleanup_processes

  echo "GRAPH_CHURN_START domain=${ROS_DOMAIN_ID} rounds=${ROUNDS} rmw=${RMW_IMPLEMENTATION} mode=rclpy_action"

  "${py}" - "${ROUNDS}" "${LOG}" <<'PY_EOF'
import gc
import os
import sys
import time

rounds = int(sys.argv[1])
log_dir = sys.argv[2]
observe_timeout = float(os.environ.get("RMW_MDDS_GRAPH_CHURN_OBSERVE_TIMEOUT", "5.0"))
min_seconds = float(os.environ.get("RMW_MDDS_GRAPH_CHURN_MIN_SECONDS", "0"))
progress_interval = int(os.environ.get("RMW_MDDS_GRAPH_CHURN_PROGRESS_INTERVAL", "1"))
spin_timeout = float(os.environ.get("RMW_MDDS_GRAPH_CHURN_SPIN_TIMEOUT", "0.02"))
poll_sleep = float(os.environ.get("RMW_MDDS_GRAPH_CHURN_POLL_SLEEP", "0.01"))
emit_timing = os.environ.get("RMW_MDDS_GRAPH_CHURN_TIMING", "0") == "1"

import rclpy
from example_interfaces.action import Fibonacci
from rclpy.action import ActionClient
from rclpy.action import ActionServer
from rclpy.action import get_action_client_names_and_types_by_node
from rclpy.action import get_action_names_and_types
from rclpy.action import get_action_server_names_and_types_by_node
from rclpy.node import Node


def execute_goal(goal_handle):
    goal_handle.succeed()
    result = Fibonacci.Result()
    result.sequence = [0, 1]
    return result


def action_names(node):
    return {name for name, _types in get_action_names_and_types(node)}


def action_server_names_by_node(node, remote_node_name):
    try:
        return {
            name
            for name, _types in get_action_server_names_and_types_by_node(
                node, remote_node_name, "/"
            )
        }
    except Exception:
        return set()


def action_client_names_by_node(node, remote_node_name):
    try:
        return {
            name
            for name, _types in get_action_client_names_and_types_by_node(
                node, remote_node_name, "/"
            )
        }
    except Exception:
        return set()


def spin_action_nodes(server_node, client_node):
    rclpy.spin_once(server_node, timeout_sec=0.0)
    rclpy.spin_once(client_node, timeout_sec=0.0)


def spin_until(observer, server_node_name, client_node_name, predicate, timeout_sec):
    deadline = time.monotonic() + timeout_sec
    last = None
    while time.monotonic() < deadline:
        rclpy.spin_once(observer, timeout_sec=spin_timeout)
        global_names = action_names(observer)
        server_names = action_server_names_by_node(observer, server_node_name)
        client_names = action_client_names_by_node(observer, client_node_name)
        last = (global_names, server_names, client_names)
        if predicate(global_names, server_names, client_names):
            return True, last
        time.sleep(poll_sleep)
    return False, last


def should_log_round(index, target_rounds, elapsed, min_elapsed):
    return (
        index == 1
        or index == target_rounds
        or index % progress_interval == 0
        or (min_elapsed > 0 and elapsed >= min_elapsed)
    )


rclpy.init(args=None)
rmw = rclpy.get_rmw_implementation_identifier()
pass_count = 0
fail_count = 0
created_actions = 0
destroyed_actions = 0
first_failure = ""
started = time.monotonic()

try:
    for i in range(1, rounds + 1):
        round_start = time.monotonic()
        action = f"/mdds_graph_churn_action_{i}"
        server_node_name = f"mdds_graph_churn_action_server_{i}"
        client_node_name = f"mdds_graph_churn_action_client_{i}"
        observer = Node(f"mdds_graph_churn_action_observer_{i}")
        server_node = Node(server_node_name)
        client_node = Node(client_node_name)
        action_server = ActionServer(server_node, Fibonacci, action, execute_goal)
        action_client = ActionClient(client_node, Fibonacci, action)
        spin_action_nodes(server_node, client_node)

        found, before = spin_until(
            observer,
            server_node_name,
            client_node_name,
            lambda global_names, server_names, client_names: (
                action in global_names
                and action in server_names
                and action in client_names
            ),
            observe_timeout,
        )
        found_done = time.monotonic()
        if found:
            created_actions += 1

        action_client.destroy()
        action_client = None
        action_server.destroy()
        action_server = None
        client_node.destroy_node()
        client_node = None
        server_node.destroy_node()
        server_node = None
        observer.destroy_node()
        observer = None
        gc.collect()
        destroy_done = time.monotonic()

        observer_after = Node(f"mdds_graph_churn_action_observer_after_{i}")
        gone, after = spin_until(
            observer_after,
            server_node_name,
            client_node_name,
            lambda global_names, server_names, client_names: (
                action not in global_names
                and action not in server_names
                and action not in client_names
            ),
            observe_timeout,
        )
        observer_after.destroy_node()
        observer_after = None
        gc.collect()
        gone_done = time.monotonic()
        if gone:
            destroyed_actions += 1

        if emit_timing:
            print(
                "ROUND_TIMING|"
                f"{i}|found_wait={found_done - round_start:.3f}|"
                f"destroy_wait={destroy_done - found_done:.3f}|"
                f"gone_wait={gone_done - destroy_done:.3f}|"
                f"total={gone_done - round_start:.3f}",
                flush=True,
            )

        elapsed_now = time.monotonic() - started
        if found and gone:
            pass_count += 1
            if should_log_round(i, rounds, elapsed_now, min_seconds):
                print(
                    f"ROUND|{i}|PASS|action={action}|server_node=/{server_node_name}|"
                    f"client_node=/{client_node_name}|elapsed_sec={elapsed_now:.3f}",
                    flush=True,
                )
        else:
            fail_count += 1
            if not first_failure:
                first_failure = (
                    f"round={i} found={int(found)} gone={int(gone)} "
                    f"before={before!r} after={after!r}"
                )
            print(
                f"ROUND|{i}|FAIL|found={int(found)}|gone={int(gone)}|"
                f"action={action}|server_node=/{server_node_name}|"
                f"client_node=/{client_node_name}",
                flush=True,
            )
            break
        if min_seconds > 0 and elapsed_now >= min_seconds:
            break
finally:
    rclpy.shutdown()

elapsed_sec = time.monotonic() - started
duration_met = min_seconds <= 0 or elapsed_sec >= min_seconds
summary_path = os.path.join(log_dir, "summary.txt")
with open(summary_path, "w", encoding="utf-8") as summary:
    summary.write("GRAPH_CHURN_MODE=rclpy_action\n")
    summary.write(f"GRAPH_CHURN_TARGET_ROUNDS={rounds}\n")
    summary.write(f"GRAPH_CHURN_ROUNDS={pass_count}\n")
    summary.write(f"GRAPH_CHURN_PASS={pass_count}\n")
    summary.write(f"GRAPH_CHURN_FAIL={fail_count}\n")
    summary.write(f"GRAPH_CHURN_ACTIONS_CREATED={created_actions}\n")
    summary.write(f"GRAPH_CHURN_ACTIONS_DESTROYED={destroyed_actions}\n")
    summary.write(f"GRAPH_CHURN_MIN_SECONDS={min_seconds:.3f}\n")
    summary.write(f"GRAPH_CHURN_ELAPSED_SEC={elapsed_sec:.3f}\n")
    summary.write(f"GRAPH_CHURN_DURATION_MET={int(duration_met)}\n")
    summary.write(f"GRAPH_CHURN_PROGRESS_INTERVAL={progress_interval}\n")
    summary.write(f"RMW_IMPLEMENTATION={rmw}\n")
    summary.write(f"ROS_DOMAIN_ID={os.environ.get('ROS_DOMAIN_ID', '')}\n")
    if first_failure:
        summary.write(f"FIRST_FAILURE={first_failure}\n")

with open(summary_path, "r", encoding="utf-8") as summary:
    print(summary.read(), end="", flush=True)

if fail_count == 0 and duration_met:
    print(
        "RESULT|rmw_mdds_board_graph_churn_action|PASS|"
        f"rounds={pass_count}|actions_created={created_actions}|"
        f"actions_destroyed={destroyed_actions}|elapsed_sec={elapsed_sec:.3f}",
        flush=True,
    )
    print("rmw_mdds_board_graph_churn_action_ok", flush=True)
    sys.exit(0)

print(
    "RESULT|rmw_mdds_board_graph_churn_action|FAIL|"
    f"rounds={pass_count}|target_rounds={rounds}|pass={pass_count}|fail={fail_count}|"
    f"actions_created={created_actions}|actions_destroyed={destroyed_actions}|"
    f"elapsed_sec={elapsed_sec:.3f}|duration_met={int(duration_met)}",
    flush=True,
)
sys.exit(1)
PY_EOF
}

run_node_list() {
  timeout "${CLI_TIMEOUT}" "${PFX}/bin/ros2" node list --no-daemon
}

run_topic_list() {
  timeout "${CLI_TIMEOUT}" "${PFX}/bin/ros2" topic list --no-daemon
}

run_service_list() {
  timeout "${CLI_TIMEOUT}" "${PFX}/bin/ros2" service list --no-daemon
}

if [ "${MODE}" = "rclpy" ]; then
  run_rclpy_graph_churn
  exit $?
fi

if [ "${MODE}" = "rclpy_action" ]; then
  run_rclpy_action_graph_churn
  exit $?
fi

rm -rf "${LOG}"
mkdir -p "${LOG}"
cleanup_processes

pass=0
fail=0
created_topics=0
created_services=0
destroyed_topics=0
destroyed_services=0

echo "GRAPH_CHURN_START domain=${ROS_DOMAIN_ID} rounds=${ROUNDS} rmw=${RMW_IMPLEMENTATION} cli_timeout=${CLI_TIMEOUT}"

i=1
while [ "${i}" -le "${ROUNDS}" ]; do
  topic_node_base="mdds_graph_churn_talker_${i}"
  service_node_base="mdds_graph_churn_service_${i}"
  topic_node="/${topic_node_base}"
  service_node="/${service_node_base}"
  topic="/mdds_graph_churn_topic_${i}"
  service="/mdds_graph_churn_add_two_ints_${i}"
  talker_log="${LOG}/talker_${i}.log"
  service_log="${LOG}/service_${i}.log"
  nodes_log="${LOG}/nodes_${i}.log"
  topics_log="${LOG}/topics_${i}.log"
  services_log="${LOG}/services_${i}.log"
  nodes_after_log="${LOG}/nodes_after_${i}.log"
  topics_after_log="${LOG}/topics_after_${i}.log"
  services_after_log="${LOG}/services_after_${i}.log"

  "${PFX}/lib/demo_nodes_cpp/talker" --ros-args -r "__node:=${topic_node_base}" -r "chatter:=${topic}" \
    >"${talker_log}" 2>&1 &
  talker_pid=$!
  "${PFX}/lib/demo_nodes_cpp/add_two_ints_server" --ros-args -r "__node:=${service_node_base}" \
    -r "add_two_ints:=${service}" >"${service_log}" 2>&1 &
  service_pid=$!

  found=0
  j=1
  while [ "${j}" -le 60 ]; do
    run_node_list >"${nodes_log}" 2>&1 || true
    run_topic_list >"${topics_log}" 2>&1 || true
    run_service_list >"${services_log}" 2>&1 || true
    if grep -qx "${topic_node}" "${nodes_log}" && grep -qx "${service_node}" "${nodes_log}" &&
      grep -qx "${topic}" "${topics_log}" && grep -qx "${service}" "${services_log}"; then
      found=1
      created_topics=$((created_topics + 1))
      created_services=$((created_services + 1))
      break
    fi
    sleep 0.5
    j=$((j + 1))
  done

  kill -9 "${talker_pid}" "${service_pid}" 2>/dev/null || true
  wait "${talker_pid}" 2>/dev/null || true
  wait "${service_pid}" 2>/dev/null || true
  cleanup_processes

  gone=0
  j=1
  while [ "${j}" -le 60 ]; do
    run_node_list >"${nodes_after_log}" 2>&1 || true
    run_topic_list >"${topics_after_log}" 2>&1 || true
    run_service_list >"${services_after_log}" 2>&1 || true
    if ! grep -qx "${topic_node}" "${nodes_after_log}" && ! grep -qx "${service_node}" "${nodes_after_log}" &&
      ! grep -qx "${topic}" "${topics_after_log}" && ! grep -qx "${service}" "${services_after_log}"; then
      gone=1
      destroyed_topics=$((destroyed_topics + 1))
      destroyed_services=$((destroyed_services + 1))
      break
    fi
    sleep 0.5
    j=$((j + 1))
  done

  if [ "${found}" = 1 ] && [ "${gone}" = 1 ]; then
    pass=$((pass + 1))
    echo "ROUND|${i}|PASS|topic_node=${topic_node}|service_node=${service_node}|topic=${topic}|service=${service}"
  else
    fail=$((fail + 1))
    echo "ROUND|${i}|FAIL|found=${found}|gone=${gone}|topic_node=${topic_node}|service_node=${service_node}|topic=${topic}|service=${service}"
    echo "--- nodes before ---"
    cat "${nodes_log}" 2>/dev/null || true
    echo "--- topics before ---"
    cat "${topics_log}" 2>/dev/null || true
    echo "--- services before ---"
    cat "${services_log}" 2>/dev/null || true
    echo "--- nodes after ---"
    cat "${nodes_after_log}" 2>/dev/null || true
    echo "--- topics after ---"
    cat "${topics_after_log}" 2>/dev/null || true
    echo "--- services after ---"
    cat "${services_after_log}" 2>/dev/null || true
    echo "--- talker log ---"
    tail -20 "${talker_log}" 2>/dev/null || true
    echo "--- service log ---"
    tail -20 "${service_log}" 2>/dev/null || true
  fi

  i=$((i + 1))
done

cat >"${LOG}/summary.txt" <<SUMMARY_EOF
GRAPH_CHURN_MODE=cli
GRAPH_CHURN_ROUNDS=${ROUNDS}
GRAPH_CHURN_PASS=${pass}
GRAPH_CHURN_FAIL=${fail}
GRAPH_CHURN_TOPICS_CREATED=${created_topics}
GRAPH_CHURN_SERVICES_CREATED=${created_services}
GRAPH_CHURN_TOPICS_DESTROYED=${destroyed_topics}
GRAPH_CHURN_SERVICES_DESTROYED=${destroyed_services}
RMW_IMPLEMENTATION=${RMW_IMPLEMENTATION}
ROS_DOMAIN_ID=${ROS_DOMAIN_ID}
CLI_TIMEOUT=${CLI_TIMEOUT}
SUMMARY_EOF

cat "${LOG}/summary.txt"

if [ "${fail}" = 0 ]; then
  echo "RESULT|rmw_mdds_board_graph_churn|PASS|rounds=${ROUNDS}|topics_created=${created_topics}|services_created=${created_services}|topics_destroyed=${destroyed_topics}|services_destroyed=${destroyed_services}"
  echo "rmw_mdds_board_graph_churn_ok"
  exit 0
fi

echo "RESULT|rmw_mdds_board_graph_churn|FAIL|rounds=${ROUNDS}|pass=${pass}|fail=${fail}|topics_created=${created_topics}|services_created=${created_services}|topics_destroyed=${destroyed_topics}|services_destroyed=${destroyed_services}"
exit 1
REMOTE_EOF

"${HDC}" -t "${DEVICE}" file send "${TMP_SCRIPT}" "${REMOTE_SCRIPT}" >/dev/null

HOST_TIMEOUT=$((ROUNDS * 90 + 120))
if [[ "${MIN_SECONDS}" -gt 0 && "${HOST_TIMEOUT}" -lt $((MIN_SECONDS + 900)) ]]; then
  HOST_TIMEOUT=$((MIN_SECONDS + 900))
fi

set +e
timeout "${HOST_TIMEOUT}s" "${HDC}" -t "${DEVICE}" shell \
  "chmod +x ${REMOTE_SCRIPT}; RMW_MDDS_GRAPH_CHURN_MODE=${MODE} RMW_MDDS_GRAPH_CHURN_CLI_TIMEOUT=${CLI_TIMEOUT} RMW_MDDS_GRAPH_CHURN_OBSERVE_TIMEOUT=${OBSERVE_TIMEOUT} RMW_MDDS_GRAPH_CHURN_MIN_SECONDS=${MIN_SECONDS} RMW_MDDS_GRAPH_CHURN_PROGRESS_INTERVAL=${PROGRESS_INTERVAL} RMW_MDDS_GRAPH_CHURN_TIMING=${TIMING} RMW_MDDS_GRAPH_DEBUG=${GRAPH_DEBUG} ${REMOTE_SCRIPT} ${DOMAIN_ID} ${ROUNDS}" >"${TMP_OUTPUT}" 2>&1
hdc_rc=$?
set -e

cat "${TMP_OUTPUT}"

if [[ "${MODE}" == "cli" ]] &&
    grep -q "RESULT|rmw_mdds_board_graph_churn|PASS|" "${TMP_OUTPUT}" &&
    grep -q "rmw_mdds_board_graph_churn_ok" "${TMP_OUTPUT}"; then
  exit 0
fi

if [[ "${MODE}" == "rclpy" ]] &&
    grep -q "RESULT|rmw_mdds_board_graph_churn_fast|PASS|" "${TMP_OUTPUT}" &&
    grep -q "rmw_mdds_board_graph_churn_fast_ok" "${TMP_OUTPUT}"; then
  exit 0
fi

if [[ "${MODE}" == "rclpy_action" ]] &&
    grep -q "RESULT|rmw_mdds_board_graph_churn_action|PASS|" "${TMP_OUTPUT}" &&
    grep -q "rmw_mdds_board_graph_churn_action_ok" "${TMP_OUTPUT}"; then
  exit 0
fi

echo "board graph churn did not produce PASS marker (hdc_rc=${hdc_rc})" >&2
echo "remote summary path: ${REMOTE_LOG}/summary.txt" >&2
exit 1
