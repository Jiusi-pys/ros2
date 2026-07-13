#!/usr/bin/env python3

import os
import re
import sys
import time


MARKER_RE = re.compile(r"^(DONE_)?CID([0-9]{2})_SEQ([0-9]{8})_")

rclpy = None
ParameterMessage = None
ParameterType = None
ParameterValue = None
SetParametersResult = None
SetParameters = None


def load_ros_interfaces():
    global rclpy
    global ParameterMessage
    global ParameterType
    global ParameterValue
    global SetParametersResult
    global SetParameters

    import rclpy as rclpy_module
    from rcl_interfaces.msg import Parameter as parameter_message
    from rcl_interfaces.msg import ParameterType as parameter_type
    from rcl_interfaces.msg import ParameterValue as parameter_value
    from rcl_interfaces.msg import SetParametersResult as set_parameters_result
    from rcl_interfaces.srv import SetParameters as set_parameters

    rclpy = rclpy_module
    ParameterMessage = parameter_message
    ParameterType = parameter_type
    ParameterValue = parameter_value
    SetParametersResult = set_parameters_result
    SetParameters = set_parameters


def safe_shutdown():
    if rclpy is None:
        return
    try:
        rclpy.shutdown()
    except Exception:
        pass


def error_text(exc):
    return str(exc).replace("\n", " ").replace("|", "/")[:300]


def payload_bounds(size, tag):
    return f"COV{tag}_START_LEN{size}_", f"_COV{tag}_END_LEN{size}"


def make_request_payload(size, client_id, sequence, done=False):
    prefix, suffix = payload_bounds(size, "SVCREQ")
    marker_prefix = "DONE_" if done else ""
    marker = f"{marker_prefix}CID{client_id:02d}_SEQ{sequence:08d}_"
    if size < len(marker):
        raise ValueError("request payload body is smaller than its correlation marker")
    body = marker + ("X" * (size - len(marker)))
    return prefix + body + suffix


def make_response_payload(size):
    prefix, suffix = payload_bounds(size, "SVCRESP")
    return prefix + ("R" * size) + suffix


def valid_payload(data, size, tag):
    prefix, suffix = payload_bounds(size, tag)
    return (
        len(data) == len(prefix) + size + len(suffix)
        and data.startswith(prefix)
        and data.endswith(suffix)
    )


def parse_request_marker(data, size):
    prefix, _ = payload_bounds(size, "SVCREQ")
    if not data.startswith(prefix):
        return None
    match = MARKER_RE.match(data[len(prefix):])
    if match is None:
        return None
    return bool(match.group(1)), int(match.group(2)), int(match.group(3))


def make_request(size, client_id, sequence, done=False):
    parameter = ParameterMessage()
    parameter.name = "payload"
    parameter.value = ParameterValue(
        type=ParameterType.PARAMETER_STRING,
        string_value=make_request_payload(size, client_id, sequence, done),
    )
    request = SetParameters.Request()
    request.parameters = [parameter]
    return request


def wait_future(node, future, timeout):
    deadline = time.monotonic() + timeout
    while not future.done() and time.monotonic() < deadline:
        rclpy.spin_once(node, timeout_sec=0.1)
    return future.done()


def valid_response(response, size):
    if response is None or not response.results:
        return False
    result = response.results[0]
    return bool(result.successful) and valid_payload(result.reason, size, "SVCRESP")


def run_server(size, service_name, expected_clients, timeout):
    node = rclpy.create_node(f"large_soak_server_{os.getpid()}")
    counts = {
        "requests": 0,
        "valid": 0,
        "invalid": 0,
        "duplicates": 0,
        "send_errors": 0,
    }
    seen = set()
    done_clients = set()
    response_payload = make_response_payload(size)

    def on_request(request, response):
        request_payload = ""
        if request.parameters:
            request_payload = request.parameters[0].value.string_value
        marker = parse_request_marker(request_payload, size)
        is_valid = valid_payload(request_payload, size, "SVCREQ") and marker is not None
        if not is_valid:
            counts["invalid"] += 1
        else:
            is_done, client_id, sequence = marker
            if is_done:
                done_clients.add(client_id)
                print(
                    f"SOAK_SERVER_CLIENT_DONE client={client_id} sequence={sequence} "
                    f"done_clients={len(done_clients)}",
                    flush=True,
                )
            else:
                counts["requests"] += 1
                key = (client_id, sequence)
                if key in seen:
                    counts["duplicates"] += 1
                else:
                    seen.add(key)
                    counts["valid"] += 1
                if counts["requests"] == 1 or counts["requests"] % 25 == 0:
                    print(
                        f"SOAK_SERVER_PROGRESS requests={counts['requests']} "
                        f"valid={counts['valid']} invalid={counts['invalid']} "
                        f"duplicates={counts['duplicates']} "
                        f"done_clients={len(done_clients)}",
                        flush=True,
                    )

        result = SetParametersResult()
        result.successful = is_valid
        result.reason = response_payload
        response.results = [result]
        return response

    node.create_service(SetParameters, service_name, on_request)
    deadline = time.monotonic() + timeout
    all_done_at = None
    while time.monotonic() < deadline:
        try:
            rclpy.spin_once(node, timeout_sec=0.1)
        except Exception as exc:
            counts["send_errors"] += 1
            print(
                f"SOAK_SERVER_SEND_EXCEPTION type={type(exc).__name__} "
                f"message={error_text(exc)}",
                flush=True,
            )
            break
        if len(done_clients) == expected_clients:
            if all_done_at is None:
                all_done_at = time.monotonic()
            elif time.monotonic() - all_done_at >= 10.0:
                break

    print(
        f"SOAK_SERVER_DONE requests={counts['requests']} valid={counts['valid']} "
        f"invalid={counts['invalid']} duplicates={counts['duplicates']} "
        f"send_errors={counts['send_errors']} done_clients={len(done_clients)} "
        f"expected_clients={expected_clients}",
        flush=True,
    )
    node.destroy_node()
    safe_shutdown()
    passed = (
        counts["requests"] > 0
        and counts["valid"] == counts["requests"]
        and counts["invalid"] == 0
        and counts["duplicates"] == 0
        and counts["send_errors"] == 0
        and len(done_clients) == expected_clients
    )
    return 0 if passed else 1


def run_client(size, service_name, client_id, duration, request_timeout):
    node = rclpy.create_node(f"large_soak_client_{client_id}_{os.getpid()}")
    client = node.create_client(SetParameters, service_name)
    if not client.wait_for_service(timeout_sec=180.0):
        print(
            f"SOAK_CLIENT_DONE client={client_id} sent=0 ok=0 valid=0 "
            "timeouts=0 errors=1 done_ack=0 elapsed_sec=0 "
            "reason=service_unavailable",
            flush=True,
        )
        node.destroy_node()
        safe_shutdown()
        return 1

    sent = 0
    ok = 0
    valid = 0
    timeouts = 0
    errors = 0
    done_ack = 0
    sequence = 1
    started = time.monotonic()
    deadline = started + duration
    last_progress = started

    while time.monotonic() < deadline:
        try:
            future = client.call_async(make_request(size, client_id, sequence))
            sent += 1
        except Exception as exc:
            errors += 1
            print(
                f"SOAK_CLIENT_SEND_EXCEPTION client={client_id} sequence={sequence} "
                f"type={type(exc).__name__} message={error_text(exc)}",
                flush=True,
            )
            break

        try:
            completed = wait_future(node, future, request_timeout)
        except Exception as exc:
            errors += 1
            print(
                f"SOAK_CLIENT_SPIN_EXCEPTION client={client_id} sequence={sequence} "
                f"type={type(exc).__name__} message={error_text(exc)}",
                flush=True,
            )
            break
        if not completed:
            timeouts += 1
            print(
                f"SOAK_CLIENT_TIMEOUT client={client_id} sequence={sequence}",
                flush=True,
            )
            break

        try:
            response = future.result()
        except Exception as exc:
            errors += 1
            print(
                f"SOAK_CLIENT_RESULT_EXCEPTION client={client_id} sequence={sequence} "
                f"type={type(exc).__name__} message={error_text(exc)}",
                flush=True,
            )
            break
        ok += 1
        if valid_response(response, size):
            valid += 1
        else:
            errors += 1
            print(
                f"SOAK_CLIENT_INVALID_RESPONSE client={client_id} sequence={sequence}",
                flush=True,
            )
            break

        now = time.monotonic()
        if sent == 1 or sent % 10 == 0 or now - last_progress >= 60.0:
            print(
                f"SOAK_CLIENT_PROGRESS client={client_id} sent={sent} ok={ok} "
                f"valid={valid} timeouts={timeouts} errors={errors} "
                f"elapsed_sec={now - started:.3f}",
                flush=True,
            )
            last_progress = now
        sequence += 1

    if timeouts == 0 and errors == 0:
        try:
            done_future = client.call_async(
                make_request(size, client_id, sequence, done=True)
            )
            if (
                wait_future(node, done_future, request_timeout)
                and valid_response(done_future.result(), size)
            ):
                done_ack = 1
            else:
                errors += 1
                print(
                    f"SOAK_CLIENT_DONE_ACK_FAILED client={client_id} "
                    f"sequence={sequence}",
                    flush=True,
                )
        except Exception as exc:
            errors += 1
            print(
                f"SOAK_CLIENT_DONE_EXCEPTION client={client_id} sequence={sequence} "
                f"type={type(exc).__name__} message={error_text(exc)}",
                flush=True,
            )

    elapsed = time.monotonic() - started
    print(
        f"SOAK_CLIENT_DONE client={client_id} sent={sent} ok={ok} valid={valid} "
        f"timeouts={timeouts} errors={errors} done_ack={done_ack} "
        f"elapsed_sec={elapsed:.3f}",
        flush=True,
    )
    node.destroy_node()
    safe_shutdown()
    passed = (
        sent > 0
        and sent == ok
        and sent == valid
        and timeouts == 0
        and errors == 0
        and done_ack == 1
        and elapsed >= duration
    )
    return 0 if passed else 1


def self_test():
    size = 128
    regular = make_request_payload(size, 3, 17)
    done = make_request_payload(size, 3, 18, done=True)
    response = make_response_payload(size)
    assert valid_payload(regular, size, "SVCREQ")
    assert valid_payload(done, size, "SVCREQ")
    assert valid_payload(response, size, "SVCRESP")
    assert parse_request_marker(regular, size) == (False, 3, 17)
    assert parse_request_marker(done, size) == (True, 3, 18)
    assert not valid_payload(regular + "x", size, "SVCREQ")
    try:
        make_request_payload(1, 3, 17)
    except ValueError:
        pass
    else:
        raise AssertionError("undersized request body was accepted")
    print("rmw_mdds_large_service_soak_self_test_ok")
    return 0


def main():
    if len(sys.argv) == 2 and sys.argv[1] == "self-test":
        return self_test()
    if len(sys.argv) < 2 or sys.argv[1] not in ("server", "client"):
        print(
            "Usage: rmw_mdds_large_service_soak.py "
            "server <body-bytes> <service> <clients> <timeout> | "
            "client <body-bytes> <service> <client-id> <duration> "
            "<request-timeout> | self-test",
            file=sys.stderr,
        )
        return 2

    try:
        load_ros_interfaces()
        rclpy.init()
        if sys.argv[1] == "server" and len(sys.argv) == 6:
            return run_server(
                int(sys.argv[2]),
                sys.argv[3],
                int(sys.argv[4]),
                float(sys.argv[5]),
            )
        if sys.argv[1] == "client" and len(sys.argv) == 7:
            return run_client(
                int(sys.argv[2]),
                sys.argv[3],
                int(sys.argv[4]),
                float(sys.argv[5]),
                float(sys.argv[6]),
            )
    except (ImportError, ValueError) as exc:
        print(f"large service soak setup failed: {error_text(exc)}", file=sys.stderr)
        safe_shutdown()
        return 2

    print("invalid role arguments", file=sys.stderr)
    safe_shutdown()
    return 2


if __name__ == "__main__":
    sys.exit(main())
