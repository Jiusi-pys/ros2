#!/usr/bin/env python3
"""Functional gate for the source-built KaihongOS CPython runtime."""

from __future__ import annotations

import bz2
import ctypes
import hashlib
import json
import lzma
import os
from pathlib import Path
import platform
import select
import socket
import sqlite3
import ssl
import subprocess
import sys
import sysconfig
import tempfile
import threading
import zlib


TEST_CERTIFICATE = """-----BEGIN CERTIFICATE-----
MIIDHzCCAgegAwIBAgIUPiiG8Hv2UDe7oJyFEl2LHRJn1jYwDQYJKoZIhvcNAQEL
BQAwFDESMBAGA1UEAwwJbG9jYWxob3N0MB4XDTI2MDkwNDIyMTcwNVoXDTM2MDkw
MTIyMTcwNVowFDESMBAGA1UEAwwJbG9jYWxob3N0MIIBIjANBgkqhkiG9w0BAQEF
AAOCAQ8AMIIBCgKCAQEA3feWWnaboWnePxuAQc1KULitFrG6nR14TWkGm9amAszL
qUAdoashhVbuKtn8SN/MWyJwsz6FOxrUcxXVIy+vn0q8CylSVeNpZkkVKtSnhRoa
sqeiHMVbqhyJaEo8M70og+EwmqZTXUvdU9+qjlMHB/l0pVBvreZQ5pWrlP5xFzry
WvCcDpXwHVKAi+mZuBUcoeA30blcIEa4Rdg7rfHswC7Cv9n4ONOh9wOgNAFwtOEf
V2QwwCuDr7noQOP4IpwWmJ3JaO8MFrWS82pC2pWSpjZUcFHRLxm2Xttuxg7ccPIF
tHljwXCWcfFiaN68aRIFlOzlqlXJjQ5uTI60fbY8DQIDAQABo2kwZzAdBgNVHQ4E
FgQUc5igQ7SldJCtsqdS7pu7hSA4IswwHwYDVR0jBBgwFoAUc5igQ7SldJCtsqdS
7pu7hSA4IswwDwYDVR0TAQH/BAUwAwEB/zAUBgNVHREEDTALgglsb2NhbGhvc3Qw
DQYJKoZIhvcNAQELBQADggEBANAxPdAtYocpmDyuK8ES+Bojb1Cg91ks0XyZHkaL
Gw3GZveeoDDo+CBi5aiEzRrdyYt8JRX7q+67XGWd0YTvUpUdKuw4Y6qwfYuhAfoh
uXdZD+feMzb4uAfLDya4dSmKE4va2K3zg/ynJBJuSwNirycu758bb3gsoLvUCUFO
y77Vx2zDD7cD8H/13DX0eLDEJGGDJoS6B5F6YGcZ+lYXpe11s+YXmHx7cKX5ech9
d6eSmahDAsYX4lPFmE7zUukQNlcmd8rKQ9Dan0aXe9d/+fcmbgCE6hkOX744D6So
fnzV4RwGgO19RySrHJ+0DcDzQUJhwaEkaTJduRWmdix3fV8=
-----END CERTIFICATE-----
"""

TEST_PRIVATE_KEY = """-----BEGIN PRIVATE KEY-----
MIIEvAIBADANBgkqhkiG9w0BAQEFAASCBKYwggSiAgEAAoIBAQDd95Zadpuhad4/
G4BBzUpQuK0WsbqdHXhNaQab1qYCzMupQB2hqyGFVu4q2fxI38xbInCzPoU7GtRz
FdUjL6+fSrwLKVJV42lmSRUq1KeFGhqyp6IcxVuqHIloSjwzvSiD4TCaplNdS91T
36qOUwcH+XSlUG+t5lDmlauU/nEXOvJa8JwOlfAdUoCL6Zm4FRyh4DfRuVwgRrhF
2Dut8ezALsK/2fg406H3A6A0AXC04R9XZDDAK4OvuehA4/ginBaYnclo7wwWtZLz
akLalZKmNlRwUdEvGbZe227GDtxw8gW0eWPBcJZx8WJo3rxpEgWU7OWqVcmNDm5M
jrR9tjwNAgMBAAECggEACCb6v7HRf3kq73hsGn6WtyZBPS8j4nddnsI3uuuER2AM
Ltgq/nARmBscPjipWmfV0pcOOpcWP5h5qwxnOpaaxafyBhrrajoi+d2/SEZtLKdL
ybn8a0AYYMQRi+IGGgRdg5J2vYdUUn3h0B3L3tRP0swnq5ars2BdIkrm9V7u+mJf
UBiBXRXgRbi+xf7/HfmHcQJJhZKhnJNwr9mV07/r8NZHz+/QMh04UfmMHxlK5ygI
HofyO+mxF/qF5/D7GHRLXgTTG5M1nN36RHlarYX6VceVQaq8DETug/aD5KVGwOzQ
ZiTEmw7vnEcN+OFT8IAQM0yQEo4MYZgpDlpzoxsWgQKBgQD8s4pTiIDxdntfgToa
iOD52mBRAnZkBPf+1AxZkW9dUyNJotb3Fkmdk92P0OukrNLm9BKJkekvtZYDFA00
oy4982qei1LPQmuVFXFlOdezds3/NpO22zwSdXcJokj0SPEOYxQG/qwqtIthm7w8
GQjdQDD1mU7m7LeFR53veNAkbQKBgQDg3Vdzbc6nseZJDy1N8cRq1O3skViKN+lx
8S65BauKWLUK56kefzzczU54H0rDJ/gs13QH+uWVolSeZhIbTUsxa22LLp7FK4t+
MPhslYEoRyI9e0JVkVeMNJU6XgnTgG6VyygFdeuYYwf1G+m53qjUlwT3EtJ1VHMh
wk6u8a1yIQKBgBHkJEcFwxtVaCa634JBbqxB6c/SfM9YCrbgDH/K7DePS1BLVyzn
Rw8BCQ7Fm+ls0wHHBgj3a6sVECnnoYe4he2c7k+LTbGe4j8L5ZtlHQB3yN3o30xy
+S3VYzgrZT7mayq5mRFltorPfY7Ll+gpXZdMlCrPT+bJm7Sz/VqXEyWBAoGAGrNt
vPEfBt6i/63jrUu2DRF3pw2jO9Zjy/ndmG7J7cWWydK0TEDDk1x1ouHkWMQYPgrf
Zksuk9QQxDZOlBtbgGTHPy2sALGpALUD6rDeA1BfCnnmaI63nJhp1+JuvESV3Qeg
mvVjolawDTThTgbYeVXtawE7KF98xFd0TGW6OMECgYBVqKiwaDUxOEQPZ7Jz04uE
1bwE7wInzIgcZvMRfJwxPnAd/2SS1x6nO67eG6bbnoYSqWsV2RgpgdvS4NYwjss0
KWHayUYpeQhNn1zDhtwxfHXvlyj62uQCmeNVsKJx3aw1bjrLx6hUhVvkGFcI9hLc
gJDRgcp83i6IeIXSh4SeTw==
-----END PRIVATE KEY-----
"""


def verify_local_tls() -> str:
    """Exercise a verified TLS handshake without consulting ambient trust."""
    with tempfile.TemporaryDirectory(prefix="python312-tls-", dir="/data/local/tmp") as temp:
        cert = Path(temp) / "cert.pem"
        key = Path(temp) / "key.pem"
        cert.write_text(TEST_CERTIFICATE, encoding="ascii")
        key.write_text(TEST_PRIVATE_KEY, encoding="ascii")

        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        errors: list[BaseException] = []

        def serve() -> None:
            try:
                context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
                context.load_cert_chain(certfile=cert, keyfile=key)
                connection, _ = listener.accept()
                with connection, context.wrap_socket(connection, server_side=True) as secured:
                    assert secured.recv(4) == b"ping"
                    secured.sendall(b"pong")
            except BaseException as exc:  # surfaced in the calling thread below
                errors.append(exc)

        worker = threading.Thread(target=serve)
        worker.start()
        client_context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        client_context.load_verify_locations(cafile=cert)
        with socket.create_connection(listener.getsockname(), timeout=5.0) as raw:
            with client_context.wrap_socket(raw, server_hostname="localhost") as secured:
                secured.sendall(b"ping")
                assert secured.recv(4) == b"pong"
                protocol = secured.version()
        worker.join(timeout=5.0)
        listener.close()
        assert not worker.is_alive()
        if errors:
            raise errors[0]
        return protocol


def main() -> None:
    assert sys.version_info[:3] == (3, 12, 7), sys.version
    assert sysconfig.get_config_var("SOABI") == "cpython-312-aarch64-linux-ohos"
    assert sysconfig.get_config_var("MULTIARCH") == "aarch64-linux-ohos"
    assert platform.machine() in {"aarch64", "arm64"}

    libc = ctypes.CDLL("libc.so")
    libc.getpid.restype = ctypes.c_int
    assert libc.getpid() == os.getpid()

    left, right = socket.socketpair()
    try:
        left.sendall(b"ohos-python-socket")
        readable, _, _ = select.select([right], [], [], 2.0)
        assert readable == [right]
        assert right.recv(64) == b"ohos-python-socket"
    finally:
        left.close()
        right.close()

    ipv6_socket_created = False
    if socket.has_ipv6:
        probe = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
        probe.close()
        ipv6_socket_created = True

    subprocess.run(["/system/bin/true"], check=True)

    payload = b"KaihongOS CPython source runtime\0" * 32
    assert zlib.decompress(zlib.compress(payload)) == payload
    assert bz2.decompress(bz2.compress(payload)) == payload
    assert lzma.decompress(lzma.compress(payload)) == payload

    connection = sqlite3.connect(":memory:")
    try:
        connection.execute("create table probe(value integer)")
        connection.execute("insert into probe values (3588)")
        assert connection.execute("select value from probe").fetchone() == (3588,)
    finally:
        connection.close()

    assert "OpenSSL 3.0.16" in ssl.OPENSSL_VERSION
    tls_protocol = verify_local_tls()
    assert hashlib.sha256(payload).hexdigest() == (
        "972a820b9b68e4dfaa8ac93ee8070f40bb950c94fe11ef536985d04bd73eb0d4"
    )

    assert not any(Path(sys.prefix).rglob("*.pyc"))
    print(
        json.dumps(
            {
                "arch": platform.machine(),
                "ipv6_socket_created": ipv6_socket_created,
                "openssl": ssl.OPENSSL_VERSION,
                "prefix": sys.prefix,
                "python": platform.python_version(),
                "soabi": sysconfig.get_config_var("SOABI"),
                "sqlite": sqlite3.sqlite_version,
                "tls_local_verified_handshake": tls_protocol,
                "verdict": "PASS",
            },
            sort_keys=True,
        )
    )


if __name__ == "__main__":
    main()
