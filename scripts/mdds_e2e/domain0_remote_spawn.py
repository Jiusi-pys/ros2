#!/usr/bin/env python3
"""Detach one guarded board payload from HDC without losing exact ownership."""

import os
import sys


def main() -> int:
    if len(sys.argv) < 3:
        return 64

    first = os.fork()
    if first < 0:
        return 70
    if first > 0:
        _, status = os.waitpid(first, 0)
        return os.waitstatus_to_exitcode(status)

    try:
        os.setsid()
        second = os.fork()
        if second < 0:
            os._exit(70)
        if second > 0:
            os._exit(0)

        os.setpgid(0, 0)
        if os.getpgrp() != os.getpid():
            os._exit(70)

        null_fd = os.open("/dev/null", os.O_RDWR)
        for target in (0, 1, 2):
            os.dup2(null_fd, target)
        if null_fd > 2:
            os.close(null_fd)
        os.execv("/bin/sh", ["sh", *sys.argv[1:]])
    except BaseException:
        os._exit(127)
    return 127


if __name__ == "__main__":
    raise SystemExit(main())
