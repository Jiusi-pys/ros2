#!/usr/bin/env python3
"""Run LTTng in a private mount namespace without touching existing daemons.

Invoke via `unshare -m python3.12 -I -B trace_mount_namespace.py PROGRAM ...`.
The caller must already be in a new mount namespace; fail if it shares the
namespace of PID 1. Only the temporary namespace receives the new mounts.
"""
import ctypes
import os
import sys

if len(sys.argv) < 2:
    raise SystemExit("missing program")
if os.readlink("/proc/self/ns/mnt") == os.readlink("/proc/1/ns/mnt"):
    raise SystemExit("refusing to mount in the system namespace; use unshare -m")
libc = ctypes.CDLL(None, use_errno=True)
mount = libc.mount
mount.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
                  ctypes.c_ulong, ctypes.c_void_p]
mount.restype = ctypes.c_int


def checked_mount(source, target, filesystem, flags, data):
    if mount(source, target, filesystem, flags, data) != 0:
        error = ctypes.get_errno()
        raise OSError(error, os.strerror(error), target.decode())


# Detach propagation before mounting anything. MS_REC | MS_PRIVATE.
checked_mount(None, b"/", None, 16384 | (1 << 18), None)
checked_mount(b"tmpfs", b"/var/run", b"tmpfs", 0, b"mode=0755,size=16m")
checked_mount(b"tmpfs", b"/dev/shm", b"tmpfs", 0, b"mode=1777,size=128m")
print("TRACE_PRIVATE_NAMESPACE " + os.readlink("/proc/self/ns/mnt"), flush=True)
os.execvpe(sys.argv[1], sys.argv[1:], os.environ)
