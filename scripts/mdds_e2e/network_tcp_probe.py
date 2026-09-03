#!/usr/bin/env python3
"""Bounded TCP reachability probe used by the GW-ISO topology gate.

The probe deliberately reports the selected local source address.  Before the
temporary Board-B address removal, the gate requires that the connection to
the PC use 192.168.8.111; after removal the same bound listener must be
unreachable.  This is a scoped direct-reachability check, not a claim about
every possible routed path on the host.
"""

import argparse
import ipaddress
import socket
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", required=True, type=int)
    parser.add_argument("--timeout", default=3.0, type=float)
    parser.add_argument("--expect", choices=("connect", "fail"), required=True)
    parser.add_argument("--expect-source", default="")
    args = parser.parse_args()

    try:
        ipaddress.IPv4Address(args.host)
        if args.expect_source:
            ipaddress.IPv4Address(args.expect_source)
    except ipaddress.AddressValueError as exc:
        parser.error(str(exc))
    if not 1 <= args.port <= 65535:
        parser.error("--port must be in 1..65535")
    if not 0 < args.timeout <= 30:
        parser.error("--timeout must be in (0, 30]")

    result = "failed"
    local = "none"
    error_type = "none"
    error_number = "none"
    try:
        with socket.create_connection((args.host, args.port), timeout=args.timeout) as sock:
            source_host, source_port = sock.getsockname()[:2]
            local = f"{source_host}:{source_port}"
            result = "connected"
    except OSError as exc:
        error_type = type(exc).__name__
        error_number = str(exc.errno) if exc.errno is not None else "none"

    passed = (args.expect == "connect" and result == "connected") or \
        (args.expect == "fail" and result == "failed")
    if args.expect_source:
        passed = passed and result == "connected" and local.split(":", 1)[0] == args.expect_source

    print(
        "GW_ISO_TCP_PROBE {} expected={} result={} host={} port={} local={} "
        "error_type={} errno={}".format(
            "PASS" if passed else "FAIL",
            args.expect,
            result,
            args.host,
            args.port,
            local,
            error_type,
            error_number,
        ),
        flush=True,
    )
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
