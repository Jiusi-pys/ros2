#!/usr/bin/env python3
"""Validate the ROS Python overlay against an isolated source-built runtime.

This is the board-acceptance replacement for the frozen source-build companion
``scripts/python_source/board_overlay_probe.py``.  The companion remains part of
the hash-bound build recipe, but its legacy-prefix test also rejects the active
``/data/python312-rk3588a-verify-*`` prefix and is therefore not an acceptance
entry point.
"""

from __future__ import annotations

import ctypes
import json
import os
from pathlib import Path
import re
import ssl

import cffi
import cryptography
import cryptography.fernet
import lxml
import lxml.etree
import numpy
import psutil
import yaml


def main() -> None:
    runtime_prefix = Path(os.environ["EXPECTED_PYTHON_PREFIX"]).resolve()
    overlay = Path(os.environ["EXPECTED_PYTHON_OVERLAY"]).resolve()
    runtime_usr = runtime_prefix / "usr"
    runtime_lib = runtime_usr / "lib"

    assert os.environ["LD_LIBRARY_PATH"] == str(runtime_lib)
    assert os.environ["OPENSSL_CONF"] == str(runtime_usr / "etc/ssl/openssl.cnf")
    assert os.environ["OPENSSL_MODULES"] == str(runtime_lib / "ossl-modules")
    assert os.environ["SSL_CERT_FILE"] == str(runtime_usr / "etc/ssl/cert.pem")
    assert os.environ["SSL_CERT_DIR"] == str(runtime_usr / "etc/ssl/certs")

    modules = (cffi, cryptography, lxml, numpy, psutil, yaml)
    for module in modules:
        module_path = Path(module.__file__).resolve()
        assert module_path == overlay or overlay in module_path.parents, (
            module.__name__,
            module_path,
        )

    left = numpy.array([[1.0, 2.0], [3.0, 4.0]])
    right = numpy.array([[2.0], [1.0]])
    assert numpy.array_equal(left @ right, numpy.array([[4.0], [10.0]]))

    ffi = cffi.FFI()
    ffi.cdef("int getpid(void);")
    assert ffi.dlopen("libc.so").getpid() == os.getpid()
    assert ctypes.CDLL("libc.so").getpid() == os.getpid()
    ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    assert psutil.Process(os.getpid()).pid == os.getpid()
    assert psutil.cpu_count() and psutil.cpu_count() > 0
    assert yaml.safe_load("board: rk3588a\n") == {"board": "rk3588a"}
    assert lxml.etree.fromstring(b"<board>rk3588a</board>").text == "rk3588a"
    key = cryptography.fernet.Fernet.generate_key()
    token = cryptography.fernet.Fernet(key).encrypt(b"source-runtime")
    assert cryptography.fernet.Fernet(key).decrypt(token) == b"source-runtime"

    maps = Path("/proc/self/maps").read_text(encoding="utf-8", errors="replace")
    assert "/data/python312-rk3588a/usr/" not in maps
    mapped_verify_prefixes = set(
        re.findall(r"/data/python312-rk3588a-verify-[^/\s]+", maps)
    )
    assert mapped_verify_prefixes == {str(runtime_prefix)}, mapped_verify_prefixes

    required_runtime_dsos = (
        "libpython3.12.so.1.0",
        "libffi.so.8.3.1",
        "libssl.so.3",
        "libcrypto.so.3",
    )
    for name in required_runtime_dsos:
        assert str(runtime_lib / name) in maps, name

    print(
        json.dumps(
            {
                "cffi": cffi.__version__,
                "cryptography": cryptography.__version__,
                "mapped_verify_prefixes": sorted(mapped_verify_prefixes),
                "numpy": numpy.__version__,
                "overlay": str(overlay),
                "psutil": psutil.__version__,
                "runtime_prefix": str(runtime_prefix),
                "verdict": "PASS",
            },
            sort_keys=True,
        )
    )


if __name__ == "__main__":
    main()
