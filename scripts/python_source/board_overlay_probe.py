#!/usr/bin/env python3
"""Exercise the locked ROS Python overlay against the source-built runtime."""

from __future__ import annotations

import json
import os
from pathlib import Path
import ctypes
import ssl

import cffi
import cryptography.fernet
import lxml.etree
import numpy
import psutil
import yaml


def main() -> None:
    runtime_prefix = Path(os.environ["EXPECTED_PYTHON_PREFIX"]).resolve()
    overlay = Path(os.environ["EXPECTED_PYTHON_OVERLAY"]).resolve()
    assert os.environ["LD_LIBRARY_PATH"] == str(runtime_prefix / "usr" / "lib")

    modules = (cffi, cryptography, lxml, numpy, psutil, yaml)
    for module in modules:
        module_path = Path(module.__file__).resolve()
        assert module_path == overlay or overlay in module_path.parents, (module, module_path)

    left = numpy.array([[1.0, 2.0], [3.0, 4.0]])
    right = numpy.array([[2.0], [1.0]])
    assert numpy.array_equal(left @ right, numpy.array([[4.0], [10.0]]))

    ffi = cffi.FFI()
    ffi.cdef("int getpid(void);")
    assert ffi.dlopen("libc.so").getpid() == os.getpid()
    assert ctypes.CDLL("libc.so").getpid() == os.getpid()
    ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    process = psutil.Process(os.getpid())
    assert process.pid == os.getpid()
    assert psutil.cpu_count() and psutil.cpu_count() > 0

    assert yaml.safe_load("board: rk3588a\n") == {"board": "rk3588a"}
    assert lxml.etree.fromstring(b"<board>rk3588a</board>").text == "rk3588a"
    key = cryptography.fernet.Fernet.generate_key()
    token = cryptography.fernet.Fernet(key).encrypt(b"source-runtime")
    assert cryptography.fernet.Fernet(key).decrypt(token) == b"source-runtime"

    maps = Path("/proc/self/maps").read_text(encoding="utf-8", errors="replace")
    assert "/data/python312-rk3588a/usr/" not in maps
    assert "/data/python312-rk3588a-verify-" not in maps
    required_runtime_dsos = (
        "libpython3.12.so.1.0",
        "libffi.so.8.3.1",
        "libssl.so.3",
        "libcrypto.so.3",
    )
    for name in required_runtime_dsos:
        assert str(runtime_prefix / "usr" / "lib" / name) in maps, name

    print(
        json.dumps(
            {
                "cffi": cffi.__version__,
                "cryptography": cryptography.__version__,
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
