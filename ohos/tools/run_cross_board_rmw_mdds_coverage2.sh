#!/usr/bin/env bash
set -uo pipefail

# rmw_mdds coverage round 2 — closes the testable gaps from round 1:
#   - larger payloads (512 KiB / 1 MiB / 1.5 MiB) toward the RELIABLE 2 MiB ceiling
#   - service request/response payloads at the same 512 KiB / 1 MiB / 1.5 MiB sizes
#   - TRANSIENT_LOCAL retained-history replay to a TRUE late joiner (timing-isolated)
#   - LIVELINESS QoS (CLI-exposed; deadline/lifespan are not, would need a node)
#   - rosbag2 record + play over rmw_mdds (also exercises the serialized path)
#
# Homogeneous rmw_mdds<->rmw_mdds (board A pub -> DSoftBus -> board B echo) for the
# cross-board lanes; the rosbag2 lane is single-board (board A) record then play.
# Set RMW_MDDS_COVERAGE2_ONLY=large|service_large|transient|liveliness|bag
# to isolate a lane.
# Large payload lanes restart short-lived brokers between sizes. By default all
# payload sizes reuse the requested domain to keep the same-domain consecutive
# large-message stress pattern. Set RMW_MDDS_COVERAGE2_LARGE_DOMAIN_STRIDE=1 to
# isolate each payload size into base, base+1, and base+2 while debugging graph
# or short-lived broker state.
# Extra-case sizes are the generated body length. The helper adds validation
# prefix/suffix bytes, and the service lane also adds ROS 2 SetParameters CDR
# plus rmw_mdds service-wire metadata. For an exact 4 MiB transported payload,
# use the body lengths below.
# To probe larger boundaries without rerunning the default sizes, set:
#   RMW_MDDS_COVERAGE2_SKIP_DEFAULT_LARGE=1
#   RMW_MDDS_COVERAGE2_LARGE_EXTRA_CASES=large4m_exact:4194254:1
#   RMW_MDDS_COVERAGE2_SKIP_DEFAULT_SERVICE_LARGE=1
#   RMW_MDDS_COVERAGE2_SERVICE_EXTRA_CASES=service_large4m_wire:4194139:1
#
# Optional diagnostics:
#   RMW_MDDS_COVERAGE2_RUNTIME_ENV='RMW_MDDS_GRAPH_DEBUG=1'
# appends extra environment assignments to every rmw_mdds board command.

usage() { echo "Usage: $0 <mdds-device-A> <mdds-device-B> [domain]" >&2; }
[[ $# -ge 2 && $# -le 3 ]] || { usage; exit 2; }
A="$1"; B="$2"; DOM="${3:-85}"
[[ "$A" != "$B" ]] || { echo "ERROR: devices must differ" >&2; exit 2; }
HDC="${HDC_BIN:-hdc}"
HDC_TIMEOUT="${RMW_MDDS_COVERAGE2_HDC_TIMEOUT:-240}"
if ! [[ "${HDC_TIMEOUT}" =~ ^[0-9]+$ ]]; then
  echo "ERROR: RMW_MDDS_COVERAGE2_HDC_TIMEOUT must be a positive integer" >&2
  exit 2
fi
PFX=/data/local/tmp/ohos-colcon-rk3588a
UNDERLAY=/data/local/tmp/ohos-prefix
FASTDDS=/data/local/tmp/ohos-fastdds
BR="${RMW_MDDS_BRIDGE_LIBRARY:-${PFX}/lib/libmdds_bridge_shared.z.so}"
LOG=/data/local/tmp/coverage2
PYTHON=/data/local/release/usr/bin/python3.12
LDP="${PFX}/lib:${UNDERLAY}/lib:${FASTDDS}/lib:/data/local/release/usr/lib:/system/lib64/platformsdk:/system/lib64/chipset-pub-sdk:/system/lib64"
PY_ENV="HOME=/data/local/tmp ROS_LOG_DIR=${LOG}/roslogs PYTHONHOME=/data/local/release/usr AMENT_PREFIX_PATH=${PFX}:${UNDERLAY} CMAKE_PREFIX_PATH=${PFX}:${UNDERLAY}:${FASTDDS} COLCON_PREFIX_PATH=${PFX}:${UNDERLAY} PYTHONPATH=${PFX}/lib/python3.12/site-packages:${UNDERLAY}/lib/python3.12/site-packages:${UNDERLAY}/lib/python3.11/site-packages"
RUNTIME_ENV="${RMW_MDDS_COVERAGE2_RUNTIME_ENV:-}"
MDDS="LD_LIBRARY_PATH=${LDP} RMW_IMPLEMENTATION=rmw_mdds_cpp RMW_MDDS_BROKER=1 RMW_MDDS_BRIDGE_LIBRARY=${BR} ${RUNTIME_ENV}"

cap() {
  local output
  output="$(timeout "${HDC_TIMEOUT}s" "${HDC}" -t "$1" shell "$2" 2>&1 || true)"
  printf '%s\n' "$output" | grep -Ev 'dumped core|Segmentation fault[[:space:]]+timeout [0-9]+s' || true
}
kill_all() {
  for d in "$A" "$B"; do
    cap "$d" "
      pat='[t]opic pub|[t]opic echo|[r]os2cli|[r]mw_mdds_broker|[b]ag record|[b]ag play|[r]osbag2|[c]ov_big_payload'
      pids=\$(ps -ef | grep -E \"\${pat}\" | grep -v grep | sed -E 's/^ *[^ ]+ +([0-9]+).*/\1/')
      for pid in \${pids}; do kill -15 \"\${pid}\" 2>/dev/null; done
      for i in \$(seq 1 ${RMW_MDDS_COVERAGE2_STOP_WAIT:-12}); do
        pids=\$(ps -ef | grep -E \"\${pat}\" | grep -v grep | sed -E 's/^ *[^ ]+ +([0-9]+).*/\1/')
        [ -z \"\${pids}\" ] && break
        sleep 1
      done
      pids=\$(ps -ef | grep -E \"\${pat}\" | grep -v grep | sed -E 's/^ *[^ ]+ +([0-9]+).*/\1/')
      for pid in \${pids}; do kill -9 \"\${pid}\" 2>/dev/null; done
      true
    " >/dev/null
  done
}
trap kill_all EXIT
PASS=0; FAIL=0
ONLY="${RMW_MDDS_COVERAGE2_ONLY:-all}"
case "$ONLY" in
  all|large|service_large|transient|liveliness|bag) ;;
  *) echo "ERROR: RMW_MDDS_COVERAGE2_ONLY must be one of: all, large, service_large, transient, liveliness, bag" >&2; exit 2 ;;
esac
run_lane_group() { [[ "$ONLY" == "all" || "$ONLY" == "$1" ]]; }
parse_extra_case() {
  local spec="$1" prefix="$2" extra
  CASE_NAME=""; CASE_SIZE=""; CASE_TIMES=""
  IFS=':' read -r CASE_NAME CASE_SIZE CASE_TIMES extra <<< "${spec}"
  if [[ -n "${extra:-}" || -z "${CASE_NAME}" || -z "${CASE_SIZE}" || -z "${CASE_TIMES}" ]]; then
    echo "ERROR: invalid extra case '${spec}', expected ${prefix}name:size:times" >&2
    exit 2
  fi
  if ! [[ "${CASE_NAME}" =~ ^[A-Za-z0-9_]+$ ]]; then
    echo "ERROR: extra case name must contain only letters, digits, and underscores: ${CASE_NAME}" >&2
    exit 2
  fi
  if [[ "${CASE_NAME}" != ${prefix}* ]]; then
    echo "ERROR: extra case name '${CASE_NAME}' must start with '${prefix}'" >&2
    exit 2
  fi
  if ! [[ "${CASE_SIZE}" =~ ^[0-9]+$ && "${CASE_SIZE}" -gt 0 &&
          "${CASE_TIMES}" =~ ^[0-9]+$ && "${CASE_TIMES}" -gt 0 ]]; then
    echo "ERROR: extra case size and times must be positive integers: ${spec}" >&2
    exit 2
  fi
}

HELPER="$(mktemp)"
cat > "$HELPER" <<'BIGPAYLOAD'
import sys
import time
import os

import rclpy
from rcl_interfaces.msg import Parameter
from rcl_interfaces.msg import ParameterType
from rcl_interfaces.msg import ParameterValue
from rcl_interfaces.msg import SetParametersResult
from rcl_interfaces.srv import SetParameters
from std_msgs.msg import String


def safe_shutdown():
    try:
        rclpy.shutdown()
    except Exception:
        pass


def error_text(exc):
    return str(exc).replace("\n", " ").replace("|", "/")[:240]


def make_payload(size, tag, request_index=0):
    body = "X" * size
    if tag == "SVCREQ" and request_index > 0:
        marker = f"REQIDX{request_index:06d}_"
        if len(marker) <= size:
            body = marker + ("X" * (size - len(marker)))
    return f"COV{tag}_START_LEN{size}_" + body + f"_COV{tag}_END_LEN{size}"


def valid_payload(data, size, tag):
    prefix = f"COV{tag}_START_LEN{size}_"
    suffix = f"_COV{tag}_END_LEN{size}"
    return len(data) == len(prefix) + size + len(suffix) and data.startswith(prefix) and data.endswith(suffix)


def request_index_from_payload(data, size):
    prefix = f"COVSVCREQ_START_LEN{size}_"
    if not data.startswith(prefix):
        return -1
    marker = data[len(prefix):len(prefix) + len("REQIDX000000_")]
    if marker.startswith("REQIDX") and marker.endswith("_"):
        try:
            return int(marker[len("REQIDX"):-1])
        except ValueError:
            return -1
    return -1


def run_pub(size, topic, times, rate):
    node = rclpy.create_node(f"cov_bigpub_{size}_{int(time.time() * 1000) % 100000}")
    publisher = node.create_publisher(String, topic, 10)

    deadline = time.monotonic() + float(os.environ.get("COV_BIG_MATCH_TIMEOUT", "45"))
    while publisher.get_subscription_count() == 0 and time.monotonic() < deadline:
        rclpy.spin_once(node, timeout_sec=0.1)

    msg = String()
    msg.data = make_payload(size, "TOPIC")
    interval = 1.0 / rate if rate > 0.0 else 0.0
    for _ in range(times):
        publisher.publish(msg)
        rclpy.spin_once(node, timeout_sec=0.05)
        if interval > 0.0:
            time.sleep(interval)

    print(
        f"COV_BIGPUB_DONE size={size} times={times} subscriptions={publisher.get_subscription_count()}",
        flush=True,
    )
    node.destroy_node()
    safe_shutdown()


def run_sub(size, topic, min_valid, timeout):
    node = rclpy.create_node(f"cov_bigsub_{size}_{int(time.time() * 1000) % 100000}")
    counts = {"received": 0, "valid": 0}

    def on_msg(msg):
        counts["received"] += 1
        data = msg.data
        if valid_payload(data, size, "TOPIC"):
            counts["valid"] += 1

    node.create_subscription(String, topic, on_msg, 10)
    deadline = time.monotonic() + timeout
    while counts["valid"] < min_valid and time.monotonic() < deadline:
        rclpy.spin_once(node, timeout_sec=0.1)

    print(
        f"COV_BIGSUB_DONE size={size} received={counts['received']} valid={counts['valid']}",
        flush=True,
    )
    node.destroy_node()
    safe_shutdown()
    return 0 if counts["valid"] >= min_valid else 1


def make_set_parameters_request(size, request_index):
    parameter = Parameter()
    parameter.name = "payload"
    parameter.value = ParameterValue(
        type=ParameterType.PARAMETER_STRING,
        string_value=make_payload(size, "SVCREQ", request_index),
    )
    request = SetParameters.Request()
    request.parameters = [parameter]
    return request


def run_service_server(size, service_name, expected_requests, timeout):
    node = rclpy.create_node(f"cov_bigsvc_server_{size}_{int(time.time() * 1000) % 100000}")
    counts = {"requests": 0, "valid": 0}
    response_grace = float(os.environ.get("COV_BIGSVC_SERVER_RESPONSE_GRACE", "5"))

    def on_request(request, response):
        counts["requests"] += 1
        request_payload = ""
        if request.parameters:
            request_payload = request.parameters[0].value.string_value
        request_index = -1
        if request_payload:
            request_index = request_index_from_payload(request_payload, size)
        print(
            f"COV_BIGSVC_SERVER_REQ index={counts['requests']} request_index={request_index} "
            f"size={size} payload_len={len(request_payload)}",
            flush=True,
        )
        is_valid = valid_payload(request_payload, size, "SVCREQ")
        if is_valid:
            counts["valid"] += 1

        result = SetParametersResult()
        result.successful = is_valid
        result.reason = make_payload(size, "SVCRESP")
        response.results = [result]
        return response

    node.create_service(SetParameters, service_name, on_request)
    deadline = time.monotonic() + timeout
    while counts["requests"] < expected_requests and time.monotonic() < deadline:
        rclpy.spin_once(node, timeout_sec=0.1)

    drain_until = time.monotonic() + response_grace
    while time.monotonic() < drain_until:
        rclpy.spin_once(node, timeout_sec=0.1)

    print(
        f"COV_BIGSVC_SERVER_DONE size={size} requests={counts['requests']} valid={counts['valid']}",
        flush=True,
    )
    node.destroy_node()
    safe_shutdown()
    return 0 if counts["requests"] >= expected_requests and counts["valid"] >= expected_requests else 1


def run_service_client(size, service_name, times, timeout):
    node = rclpy.create_node(f"cov_bigsvc_client_{size}_{int(time.time() * 1000) % 100000}")
    client = node.create_client(SetParameters, service_name)
    sent = 0
    ok = 0
    response_valid = 0

    def print_done(service_available, error=""):
        suffix = f" error={error}" if error else ""
        print(
            f"COV_BIGSVC_CLIENT_DONE size={size} sent={sent} ok={ok} "
            f"response_valid={response_valid} service_available={service_available}{suffix}",
            flush=True,
        )

    try:
        service_ready = client.wait_for_service(timeout_sec=timeout)
    except Exception as exc:
        print(
            f"COV_BIGSVC_CLIENT_WAIT_SERVICE_EXCEPTION type={type(exc).__name__} message={error_text(exc)}",
            flush=True,
        )
        print_done(0, type(exc).__name__)
        node.destroy_node()
        safe_shutdown()
        return 1

    if not service_ready:
        print_done(0)
        node.destroy_node()
        safe_shutdown()
        return 1

    print(f"COV_BIGSVC_CLIENT_READY size={size} times={times}", flush=True)
    for index in range(1, times + 1):
        request = make_set_parameters_request(size, index)
        request_payload = request.parameters[0].value.string_value if request.parameters else ""
        print(
            f"COV_BIGSVC_CLIENT_SEND_START index={index} size={size} payload_len={len(request_payload)}",
            flush=True,
        )
        try:
            future = client.call_async(request)
        except Exception as exc:
            print(
                f"COV_BIGSVC_CLIENT_SEND_EXCEPTION index={index} type={type(exc).__name__} "
                f"message={error_text(exc)}",
                flush=True,
            )
            print_done(1, type(exc).__name__)
            node.destroy_node()
            safe_shutdown()
            return 1
        sent += 1
        print(f"COV_BIGSVC_CLIENT_SENT index={index} sent={sent}", flush=True)
        deadline = time.monotonic() + timeout
        while not future.done() and time.monotonic() < deadline:
            try:
                rclpy.spin_once(node, timeout_sec=0.1)
            except Exception as exc:
                print(
                    f"COV_BIGSVC_CLIENT_SPIN_EXCEPTION index={index} type={type(exc).__name__} "
                    f"message={error_text(exc)}",
                    flush=True,
                )
                print_done(1, type(exc).__name__)
                node.destroy_node()
                safe_shutdown()
                return 1
        if not future.done():
            print(f"COV_BIGSVC_CLIENT_WAIT_TIMEOUT index={index}", flush=True)
            continue
        try:
            response = future.result()
        except Exception as exc:
            print(
                f"COV_BIGSVC_CLIENT_RESULT_EXCEPTION index={index} type={type(exc).__name__} "
                f"message={error_text(exc)}",
                flush=True,
            )
            continue
        if not response or not response.results:
            print(f"COV_BIGSVC_CLIENT_EMPTY_RESPONSE index={index}", flush=True)
            continue
        result = response.results[0]
        if result.successful:
            ok += 1
        valid_response = valid_payload(result.reason, size, "SVCRESP")
        if valid_response:
            response_valid += 1
        print(
            f"COV_BIGSVC_CLIENT_RESP index={index} successful={int(result.successful)} "
            f"valid={int(valid_response)} reason_len={len(result.reason)}",
            flush=True,
        )

    print_done(1)
    node.destroy_node()
    safe_shutdown()
    return 0 if sent >= times and ok >= times and response_valid >= times else 1


def main():
    mode = sys.argv[1]
    size = int(sys.argv[2])
    topic = sys.argv[3]
    rclpy.init()
    if mode == "pub":
        run_pub(size, topic, int(sys.argv[4]), float(sys.argv[5]))
        return 0
    if mode == "sub":
        return run_sub(size, topic, int(sys.argv[4]), float(sys.argv[5]))
    if mode == "service_server":
        return run_service_server(size, topic, int(sys.argv[4]), float(sys.argv[5]))
    if mode == "service_client":
        return run_service_client(size, topic, int(sys.argv[4]), float(sys.argv[5]))
    print(f"unknown mode: {mode}", file=sys.stderr)
    safe_shutdown()
    return 2


if __name__ == "__main__":
    sys.exit(main())
BIGPAYLOAD
"${HDC}" -t "$A" file send "$HELPER" /data/local/tmp/cov_big_payload.py >/dev/null 2>&1
"${HDC}" -t "$B" file send "$HELPER" /data/local/tmp/cov_big_payload.py >/dev/null 2>&1
rm -f "$HELPER"

if run_lane_group large; then
  # ---- large-payload lanes (homogeneous cross-board) ----
  BIG_WARMUP="${RMW_MDDS_COVERAGE2_BIG_WARMUP:-25}"
  BIG_MATCH_TIMEOUT="${RMW_MDDS_COVERAGE2_BIG_MATCH_TIMEOUT:-45}"
  BIG_SUB_TIMEOUT="${RMW_MDDS_COVERAGE2_BIG_SUB_TIMEOUT:-180}"
  if ! [[ "${BIG_SUB_TIMEOUT}" =~ ^[0-9]+$ ]]; then
    echo "ERROR: RMW_MDDS_COVERAGE2_BIG_SUB_TIMEOUT must be a positive integer" >&2
    exit 2
  fi
  BIG_SUB_WAIT="${RMW_MDDS_COVERAGE2_BIG_SUB_WAIT:-$((BIG_SUB_TIMEOUT + 15))}"
  BIG_PUB_WAIT="${RMW_MDDS_COVERAGE2_BIG_PUB_WAIT:-120}"
  if ! [[ "${BIG_SUB_WAIT}" =~ ^[0-9]+$ && "${BIG_PUB_WAIT}" =~ ^[0-9]+$ ]]; then
    echo "ERROR: RMW_MDDS_COVERAGE2_BIG_SUB_WAIT and RMW_MDDS_COVERAGE2_BIG_PUB_WAIT must be non-negative integers" >&2
    exit 2
  fi
  BIG_DOMAIN_STRIDE="${RMW_MDDS_COVERAGE2_LARGE_DOMAIN_STRIDE:-0}"
  if ! [[ "${BIG_DOMAIN_STRIDE}" =~ ^[0-9]+$ ]]; then
    echo "ERROR: RMW_MDDS_COVERAGE2_LARGE_DOMAIN_STRIDE must be a non-negative integer" >&2
    exit 2
  fi
  big_lane() {
    local name="$1" sz="$2" times="$3" ordinal="$4"
    local lane_dom=$((DOM + ordinal * BIG_DOMAIN_STRIDE))
    kill_all; cap "$A" "mkdir -p ${LOG}/roslogs; rm -f ${LOG}/${name}_*.log; true" >/dev/null
    cap "$B" "mkdir -p ${LOG}/roslogs; rm -f ${LOG}/${name}_*.log; true" >/dev/null; sleep 2
    cap "$B" "nohup sh -c '${MDDS} ${PY_ENV} ROS_DOMAIN_ID=${lane_dom} ${PYTHON} /data/local/tmp/cov_big_payload.py sub ${sz} /cov_${name} ${times} ${BIG_SUB_TIMEOUT} > ${LOG}/${name}_e.log 2>&1' >/dev/null 2>&1 & echo s" >/dev/null
    sleep "${BIG_WARMUP}"
    cap "$A" "nohup sh -c '${MDDS} ${PY_ENV} COV_BIG_MATCH_TIMEOUT=${BIG_MATCH_TIMEOUT} ROS_DOMAIN_ID=${lane_dom} ${PYTHON} /data/local/tmp/cov_big_payload.py pub ${sz} /cov_${name} ${times} 1 > ${LOG}/${name}_p.log 2>&1' >/dev/null 2>&1 & echo p" >/dev/null
    cap "$B" "for i in \$(seq 1 ${BIG_SUB_WAIT}); do grep -q COV_BIGSUB_DONE ${LOG}/${name}_e.log 2>/dev/null && break; sleep 1; done" >/dev/null
    cap "$A" "for i in \$(seq 1 ${BIG_PUB_WAIT}); do grep -q COV_BIGPUB_DONE ${LOG}/${name}_p.log 2>/dev/null && break; sleep 1; done" >/dev/null
    local rx em; rx="$(cap "$B" "sed -n 's/.*received=\\([0-9][0-9]*\\).*/\\1/p' ${LOG}/${name}_e.log 2>/dev/null | tail -1" | tr -d '[:space:]')"
    em="$(cap "$B" "sed -n 's/.*valid=\\([0-9][0-9]*\\).*/\\1/p' ${LOG}/${name}_e.log 2>/dev/null | tail -1" | tr -d '[:space:]')"
    if [[ "${rx:-0}" =~ ^[0-9]+$ && "${rx:-0}" -ge "${times}" && "${em:-0}" =~ ^[0-9]+$ && "${em:-0}" -ge "${times}" ]]; then
      echo "RESULT|cov2_${name}|PASS|received=${rx}|valid_len=${em}"; PASS=$((PASS+1))
    else
      echo "RESULT|cov2_${name}|FAIL|received=${rx:-0}|valid_len=${em:-0}"; FAIL=$((FAIL+1))
      cap "$A" "tail -3 ${LOG}/${name}_p.log 2>/dev/null"
      cap "$B" "tail -3 ${LOG}/${name}_e.log 2>/dev/null"
    fi
  }
  run_extra_large_cases() {
    local cases="$1" ordinal="$2" spec
    [[ -z "${cases}" ]] && return 0
    local specs=()
    IFS=',' read -r -a specs <<< "${cases}"
    for spec in "${specs[@]}"; do
      parse_extra_case "${spec}" "large"
      big_lane "${CASE_NAME}" "${CASE_SIZE}" "${CASE_TIMES}" "${ordinal}"
      ordinal=$((ordinal + 1))
    done
  }

  large_ordinal=0
  if [[ "${RMW_MDDS_COVERAGE2_SKIP_DEFAULT_LARGE:-0}" != "1" ]]; then
    big_lane large512k 524288 12 "${large_ordinal}"; large_ordinal=$((large_ordinal + 1))
    big_lane large1m   1048576 10 "${large_ordinal}"; large_ordinal=$((large_ordinal + 1))
    big_lane large1500k 1572864 8 "${large_ordinal}"; large_ordinal=$((large_ordinal + 1))
  fi
  run_extra_large_cases "${RMW_MDDS_COVERAGE2_LARGE_EXTRA_CASES:-}" "${large_ordinal}"
fi

if run_lane_group service_large; then
  SVC_WARMUP="${RMW_MDDS_COVERAGE2_SERVICE_WARMUP:-20}"
  SVC_SERVER_TIMEOUT="${RMW_MDDS_COVERAGE2_SERVICE_SERVER_TIMEOUT:-180}"
  SVC_CLIENT_TIMEOUT="${RMW_MDDS_COVERAGE2_SERVICE_CLIENT_TIMEOUT:-120}"
  SVC_SERVER_RESPONSE_GRACE="${RMW_MDDS_COVERAGE2_SERVICE_SERVER_RESPONSE_GRACE:-5}"
  SVC_SERVER_WAIT="${RMW_MDDS_COVERAGE2_SERVICE_SERVER_WAIT:-$((SVC_SERVER_TIMEOUT + 15))}"
  SVC_CLIENT_WAIT="${RMW_MDDS_COVERAGE2_SERVICE_CLIENT_WAIT:-$((SVC_CLIENT_TIMEOUT + 15))}"
  SVC_DOMAIN_STRIDE="${RMW_MDDS_COVERAGE2_SERVICE_DOMAIN_STRIDE:-0}"
  if ! [[ "${SVC_SERVER_TIMEOUT}" =~ ^[0-9]+$ && "${SVC_CLIENT_TIMEOUT}" =~ ^[0-9]+$ &&
          "${SVC_SERVER_RESPONSE_GRACE}" =~ ^[0-9]+$ &&
          "${SVC_SERVER_WAIT}" =~ ^[0-9]+$ && "${SVC_CLIENT_WAIT}" =~ ^[0-9]+$ &&
          "${SVC_DOMAIN_STRIDE}" =~ ^[0-9]+$ ]]; then
    echo "ERROR: service_large timeout/wait/grace/stride settings must be non-negative integers" >&2
    exit 2
  fi
  service_lane() {
    local name="$1" sz="$2" times="$3" ordinal="$4"
    local lane_dom=$((DOM + ordinal * SVC_DOMAIN_STRIDE))
    local service="/cov_${name}"
    kill_all; cap "$A" "mkdir -p ${LOG}/roslogs; rm -f ${LOG}/${name}_*.log; true" >/dev/null
    cap "$B" "mkdir -p ${LOG}/roslogs; rm -f ${LOG}/${name}_*.log; true" >/dev/null; sleep 2
    cap "$B" "nohup sh -c '${MDDS} ${PY_ENV} COV_BIGSVC_SERVER_RESPONSE_GRACE=${SVC_SERVER_RESPONSE_GRACE} ROS_DOMAIN_ID=${lane_dom} ${PYTHON} /data/local/tmp/cov_big_payload.py service_server ${sz} ${service} ${times} ${SVC_SERVER_TIMEOUT} > ${LOG}/${name}_server.log 2>&1' >/dev/null 2>&1 & echo s" >/dev/null
    sleep "${SVC_WARMUP}"
    cap "$A" "nohup sh -c '${MDDS} ${PY_ENV} ROS_DOMAIN_ID=${lane_dom} ${PYTHON} /data/local/tmp/cov_big_payload.py service_client ${sz} ${service} ${times} ${SVC_CLIENT_TIMEOUT} > ${LOG}/${name}_client.log 2>&1' >/dev/null 2>&1 & echo c" >/dev/null
    cap "$A" "for i in \$(seq 1 ${SVC_CLIENT_WAIT}); do grep -q COV_BIGSVC_CLIENT_DONE ${LOG}/${name}_client.log 2>/dev/null && break; sleep 1; done" >/dev/null
    cap "$B" "for i in \$(seq 1 ${SVC_SERVER_WAIT}); do grep -q COV_BIGSVC_SERVER_DONE ${LOG}/${name}_server.log 2>/dev/null && break; sleep 1; done" >/dev/null
    local sent ok response_valid requests valid
    sent="$(cap "$A" "sed -n 's/.*sent=\\([0-9][0-9]*\\).*/\\1/p' ${LOG}/${name}_client.log 2>/dev/null | tail -1" | tr -d '[:space:]')"
    ok="$(cap "$A" "sed -n 's/.* ok=\\([0-9][0-9]*\\).*/\\1/p' ${LOG}/${name}_client.log 2>/dev/null | tail -1" | tr -d '[:space:]')"
    response_valid="$(cap "$A" "sed -n 's/.*response_valid=\\([0-9][0-9]*\\).*/\\1/p' ${LOG}/${name}_client.log 2>/dev/null | tail -1" | tr -d '[:space:]')"
    requests="$(cap "$B" "sed -n 's/.*requests=\\([0-9][0-9]*\\).*/\\1/p' ${LOG}/${name}_server.log 2>/dev/null | tail -1" | tr -d '[:space:]')"
    valid="$(cap "$B" "sed -n 's/.*valid=\\([0-9][0-9]*\\).*/\\1/p' ${LOG}/${name}_server.log 2>/dev/null | tail -1" | tr -d '[:space:]')"
    if [[ "${sent:-0}" =~ ^[0-9]+$ && "${sent:-0}" -eq "${times}" &&
          "${ok:-0}" =~ ^[0-9]+$ && "${ok:-0}" -eq "${times}" &&
          "${response_valid:-0}" =~ ^[0-9]+$ && "${response_valid:-0}" -eq "${times}" &&
          "${requests:-0}" =~ ^[0-9]+$ && "${requests:-0}" -eq "${times}" &&
          "${valid:-0}" =~ ^[0-9]+$ && "${valid:-0}" -eq "${times}" ]]; then
      echo "RESULT|cov2_${name}|PASS|sent=${sent}|server_req=${requests}|server_valid=${valid}|client_ok=${ok}|response_valid=${response_valid}"; PASS=$((PASS+1))
    else
      echo "RESULT|cov2_${name}|FAIL|sent=${sent:-0}|server_req=${requests:-0}|server_valid=${valid:-0}|client_ok=${ok:-0}|response_valid=${response_valid:-0}"; FAIL=$((FAIL+1))
      cap "$A" "tail -5 ${LOG}/${name}_client.log 2>/dev/null"
      cap "$B" "tail -5 ${LOG}/${name}_server.log 2>/dev/null"
    fi
  }
  run_extra_service_cases() {
    local cases="$1" ordinal="$2" spec
    [[ -z "${cases}" ]] && return 0
    local specs=()
    IFS=',' read -r -a specs <<< "${cases}"
    for spec in "${specs[@]}"; do
      parse_extra_case "${spec}" "service_large"
      service_lane "${CASE_NAME}" "${CASE_SIZE}" "${CASE_TIMES}" "${ordinal}"
      ordinal=$((ordinal + 1))
    done
  }

  service_ordinal=0
  if [[ "${RMW_MDDS_COVERAGE2_SKIP_DEFAULT_SERVICE_LARGE:-0}" != "1" ]]; then
    service_lane service_large512k 524288 3 "${service_ordinal}"; service_ordinal=$((service_ordinal + 1))
    service_lane service_large1m 1048576 2 "${service_ordinal}"; service_ordinal=$((service_ordinal + 1))
    service_lane service_large1500k 1572864 1 "${service_ordinal}"; service_ordinal=$((service_ordinal + 1))
  fi
  run_extra_service_cases "${RMW_MDDS_COVERAGE2_SERVICE_EXTRA_CASES:-}" "${service_ordinal}"
fi

# ---- TRANSIENT_LOCAL retained replay to a TRUE late joiner ----
# Publisher (transient_local, kept alive) publishes ONE sample at t~0 and stays alive.
# The subscriber joins LATE (t~12s), so any sample it has can ONLY be retained history.
# Exactly one received sample proves retained replay without duplicate endpoint-refresh replay.
if run_lane_group transient; then
  kill_all; cap "$A" "mkdir -p ${LOG}; rm -f ${LOG}/tl_*.log; true" >/dev/null
  cap "$B" "mkdir -p ${LOG}; rm -f ${LOG}/tl_*.log; true" >/dev/null; sleep 2
  TLQ="--qos-durability transient_local --qos-reliability reliable --qos-history keep_last --qos-depth 5"
  cap "$A" "nohup sh -c '${MDDS} ROS_DOMAIN_ID=${DOM} ${PFX}/bin/ros2 topic pub --times 1 -r 1 -w 0 --keep-alive 30 ${TLQ} /cov_tlreplay std_msgs/msg/String \"{data: TLRETAIN}\" > ${LOG}/tl_p.log 2>&1' >/dev/null 2>&1 & echo p" >/dev/null
  echo "--- transient_local publisher up; waiting 12s before the LATE subscriber joins ---"
  sleep 12
  cap "$B" "nohup sh -c '${MDDS} ROS_DOMAIN_ID=${DOM} ${PFX}/bin/ros2 topic echo ${TLQ} /cov_tlreplay std_msgs/msg/String --no-daemon > ${LOG}/tl_e.log 2>&1' >/dev/null 2>&1 & echo s" >/dev/null
  sleep 9   # check at pub+~21s, before the next real publish at pub+30s
  TLRX="$(cap "$B" "grep -c '^data: TLRETAIN$' ${LOG}/tl_e.log 2>/dev/null" | tr -d '[:space:]')"
  if [[ "${TLRX:-0}" =~ ^[0-9]+$ && "${TLRX:-0}" -eq 1 ]]; then
    echo "RESULT|cov2_transient_local_replay|PASS|late_joiner_got_retained=${TLRX}|expected=1"; PASS=$((PASS+1))
  else
    echo "RESULT|cov2_transient_local_replay|FAIL|late_joiner_got=${TLRX:-0}|expected=1"; FAIL=$((FAIL+1))
  fi
fi

# ---- LIVELINESS QoS ----
if run_lane_group liveliness; then
  kill_all; cap "$A" "mkdir -p ${LOG}; rm -f ${LOG}/lv_*.log; true" >/dev/null
  cap "$B" "mkdir -p ${LOG}; rm -f ${LOG}/lv_*.log; true" >/dev/null; sleep 2
  LVQ="--qos-liveliness automatic --qos-reliability reliable"
  cap "$B" "nohup sh -c '${MDDS} ROS_DOMAIN_ID=${DOM} ${PFX}/bin/ros2 topic echo ${LVQ} /cov_lv std_msgs/msg/String --no-daemon > ${LOG}/lv_e.log 2>&1' >/dev/null 2>&1 & echo s" >/dev/null
  sleep 14
  cap "$A" "nohup sh -c '${MDDS} ROS_DOMAIN_ID=${DOM} ${PFX}/bin/ros2 topic pub ${LVQ} --times 20 -r 2 -w 0 /cov_lv std_msgs/msg/String \"{data: lv}\" > ${LOG}/lv_p.log 2>&1' >/dev/null 2>&1 & echo p" >/dev/null
  sleep 18
  LVRX="$(cap "$B" "grep -c '^---' ${LOG}/lv_e.log 2>/dev/null" | tr -d '[:space:]')"
  if [[ "${LVRX:-0}" =~ ^[0-9]+$ && "${LVRX:-0}" -gt 0 ]]; then
    echo "RESULT|cov2_qos_liveliness|PASS|received=${LVRX}"; PASS=$((PASS+1))
  else
    echo "RESULT|cov2_qos_liveliness|FAIL|received=${LVRX:-0}"; FAIL=$((FAIL+1))
  fi
fi

# ---- rosbag2 record + play over rmw_mdds (single board A; also serialized path) ----
if run_lane_group bag; then
  kill_all; cap "$A" "mkdir -p ${LOG}; rm -f ${LOG}/bag_*.log; rm -rf /data/local/tmp/cov_bag; true" >/dev/null; sleep 2
  cap "$A" "nohup sh -c '${MDDS} ROS_DOMAIN_ID=${DOM} ${PFX}/bin/ros2 topic pub -r 5 -w 0 /cov_bagtopic std_msgs/msg/String \"{data: BAGDATA}\" > ${LOG}/bag_pub.log 2>&1' >/dev/null 2>&1 & echo p" >/dev/null
  sleep 8
  cap "$A" "nohup sh -c '${MDDS} ROS_DOMAIN_ID=${DOM} ${PFX}/bin/ros2 bag record --storage sqlite3 --topics /cov_bagtopic -o /data/local/tmp/cov_bag > ${LOG}/bag_rec.log 2>&1' >/dev/null 2>&1 & echo r" >/dev/null
  sleep 18
  cap "$A" "ps -ef | grep 'bag record' | grep -v grep | while read -r u pid r; do kill -2 \"\${pid}\" 2>/dev/null; done; sleep 3; ps -ef | grep -E 'bag record|topic pub' | grep -v grep | while read -r u pid r; do kill -9 \"\${pid}\" 2>/dev/null; done; true" >/dev/null
  sleep 2
  REC="$(cap "$A" "ls /data/local/tmp/cov_bag/ 2>/dev/null | grep -cE 'metadata|\\.db3|\\.mcap'")"
  REC_MSGS="$(cap "$A" "${MDDS} ROS_DOMAIN_ID=${DOM} ${PFX}/bin/ros2 bag info /data/local/tmp/cov_bag 2>/dev/null | sed -n 's/.*Messages:[[:space:]]*\\([0-9][0-9]*\\).*/\\1/p' | head -1" | tr -d '[:space:]')"
  # play phase: subscriber then bag play
  cap "$A" "nohup sh -c '${MDDS} ROS_DOMAIN_ID=${DOM} ${PFX}/bin/ros2 topic echo /cov_bagtopic std_msgs/msg/String --no-daemon > ${LOG}/bag_echo.log 2>&1' >/dev/null 2>&1 & echo s" >/dev/null
  sleep 6
  cap "$A" "${MDDS} ROS_DOMAIN_ID=${DOM} ${PFX}/bin/ros2 bag play /data/local/tmp/cov_bag > ${LOG}/bag_play.log 2>&1; true" >/dev/null
  sleep 4
  BAGRX="$(cap "$A" "grep -c BAGDATA ${LOG}/bag_echo.log 2>/dev/null" | tr -d '[:space:]')"
  if [[ "${REC:-0}" =~ ^[0-9]+$ && "${REC:-0}" -gt 0 && "${REC_MSGS:-0}" =~ ^[0-9]+$ && "${REC_MSGS:-0}" -gt 0 && "${BAGRX:-0}" =~ ^[0-9]+$ && "${BAGRX:-0}" -gt 0 ]]; then
    echo "RESULT|cov2_rosbag2_record_play|PASS|recorded_files=${REC}|recorded_messages=${REC_MSGS}|played_received=${BAGRX}"; PASS=$((PASS+1))
  else
    echo "RESULT|cov2_rosbag2_record_play|FAIL|recorded_files=${REC:-0}|recorded_messages=${REC_MSGS:-0}|played_received=${BAGRX:-0}"; FAIL=$((FAIL+1))
    echo "--- bag record log ---"; cap "$A" "tail -4 ${LOG}/bag_rec.log 2>/dev/null"
    echo "--- bag play log ---";   cap "$A" "tail -4 ${LOG}/bag_play.log 2>/dev/null"
  fi
fi

echo "COVERAGE2_SUMMARY|pass=${PASS}|fail=${FAIL}"
if (( FAIL > 0 )); then
  exit 1
fi
