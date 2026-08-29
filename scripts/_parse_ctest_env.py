#!/usr/bin/env python3
# Generate a board-side test driver script for one package from
# build_ohos/<pkg>/CTestTestfile.cmake.
#
# The ament test fixtures (env vars, LD_LIBRARY_PATH appends, skip markers)
# only exist in the generated CTest file; replaying them faithfully avoids a
# hand-maintained per-test list. Host build/install paths inside the values
# are remapped to the board layout ($ROS2_HOME/tests/<pkg>/ mirrors the
# package's build dir; $ROS2_HOME == install prefix).
#
# Output (stdout): a POSIX sh script that runs each gtest executable with
# `timeout`, writes <name>.log, and prints parseable verdict lines:
#   BOARDTEST <name> PASS|FAIL rc=<n>
# Tests registered with --skip-test (e.g. memory_tools preload unavailable at
# configure time) and tests without a native executable are emitted as
#   BOARDTEST <name> SKIP
import re
import shlex
import sys

ROS2_HOME = "/data/local/tmp/ros2"
TIMEOUT = "180"

ctest_file, pkg, ws_root = sys.argv[1], sys.argv[2], sys.argv[3]
ws_root = ws_root.replace("\\", "/")
build_base = f"{ws_root}/build_ohos"
pkg_build = f"{build_base}/{pkg}"
board_dir = f"{ROS2_HOME}/tests/{pkg}"


def remap(v: str) -> str:
    v = v.replace("\\", "/")
    v = re.sub(re.escape(build_base) + r"/([^/;:\"]+)",
               ROS2_HOME + r"/tests/\1", v)
    v = v.replace(f"{ws_root}/install_ohos", ROS2_HOME)
    # strip any remaining host-absolute path segments
    v = re.sub(r"[A-Za-z]:/[^;:\"']*", "", v)
    return v


def header():
    print("#!/bin/sh")
    print(f". {ROS2_HOME}/env.sh")
    # this gtest version hardcodes /tmp for its death-test capture files;
    # the board's /tmp is a read-only rootfs, so mount a tmpfs over it
    print("mount -t tmpfs tmpfs /tmp 2>/dev/null || true")
    print(f"export TMPDIR={ROS2_HOME}/tmp")
    print(f"mkdir -p {ROS2_HOME}/tmp")
    print(f"cd {board_dir} || exit 1")
    # helper .so files (e.g. rviz_rendering_test_utils) live next to the test
    # executables; on the build host DT_RPATH finds them, on the board the
    # test dir must be on LD_LIBRARY_PATH
    print("export LD_LIBRARY_PATH=\"$PWD:$LD_LIBRARY_PATH\"")


def verdict_line(name):
    return (f"if [ $rc -eq 0 ]; then echo 'BOARDTEST {name} PASS'; "
            f"else echo \"BOARDTEST {name} FAIL rc=$rc\"; fi")


text = open(ctest_file, encoding="utf-8").read()
header()
seen = set()
tests = list(re.finditer(r"add_test\(\[=\[(.+?)\]=\]\s*(.+?)\)\r?\n", text, re.S))
# plain add_test(NAME x COMMAND exe) form (no run_test.py wrapper)
tests += [m for m in re.finditer(
    r"add_test\(NAME\s+(\S+)\s+COMMAND\s+(.+?)\)\r?\n", text, re.S)
    if not any(t.group(1) == m.group(1) for t in tests)]
# plain add_test(name ...) form (quoted name, no NAME/COMMAND keywords)
tests += [m for m in re.finditer(
    r'add_test\((?:\[=\[)?"?([\w.-]+)"?(?:\]=\])?\s+(.+?)\)\r?\n', text, re.S)
    if not any(t.group(1) == m.group(1) for t in tests)]
for m in tests:
    name, args_blob = m.group(1), m.group(2)
    if name in seen:
        continue
    seen.add(name)
    try:
        args = shlex.split(args_blob.replace('"[=[', '"').replace(']=]"', '"'))
    except ValueError:
        continue
    if "--skip-test" in args:
        print(f"echo 'BOARDTEST {name} SKIP'")
        continue
    envs, appends, exe, cmd_args = [], [], "", []
    i = 0
    while i < len(args):
        a = args[i]
        if a in ("--env", "--append-env"):
            j = i + 1
            while j < len(args) and not args[j].startswith("--"):
                (envs if a == "--env" else appends).append(args[j])
                j += 1
            i = j
            continue
        if a == "--command" and i + 1 < len(args):
            exe = args[i + 1].replace("\\", "/")
            exe = exe[len(pkg_build) + 1:] if exe.startswith(pkg_build) \
                else exe.rsplit("/", 1)[-1]
            # everything after the exe is the test's own command line
            # (e.g. test_communication's message type + RMW name); dropping
            # it makes binaries misread the first gtest flag as positional
            cmd_args = args[i + 2:]
            break
        i += 1
    if not exe and args and not args[0].endswith(".exe") and \
            not args[0].endswith(".py"):
        # plain add_test(NAME x COMMAND x_executable) without run_test.py
        cand = args[0].replace("\\", "/")
        if cand.startswith(pkg_build):
            exe = cand[len(pkg_build) + 1:]
            cmd_args = args[1:]
        elif "/" not in cand:
            exe = cand
            cmd_args = args[1:]
    if not exe or exe.endswith(".exe") or exe.endswith(".py") or exe == "python.exe":
        # lint / python tests have no native board executable here
        print(f"echo 'BOARDTEST {name} SKIP'")
        continue
    assignments = []
    exports = []
    for e in envs:
        k, _, v = e.partition("=")
        if not k:
            continue
        assignments.append(f"{k}={remap(v)}")
    for e in appends:
        k, _, v = e.partition("=")
        if not k:
            continue
        if k in ("LD_LIBRARY_PATH", "PATH", "PYTHONPATH"):
            # env K=V does not expand $K; emit a real export so the append
            # keeps the existing value
            exports.append(f"export {k}={shlex.quote(remap(v))}\":${k}\"")
        else:
            assignments.append(f"{k}={remap(v)}")
    env_prefix = " ".join(shlex.quote(a) for a in assignments)
    for x in exports:
        print(x)
    extra = " ".join(shlex.quote(remap(a)) for a in cmd_args)
    # GTEST_BRIEF via env, not argv: tests with required positional args
    # (test_communication's message type) reject the extra argv element
    cmd = (f"env GTEST_BRIEF=1 {env_prefix} timeout {TIMEOUT} ./{shlex.quote(exe)}"
           f"{' ' + extra if extra else ''}")
    print(f"{cmd} > {shlex.quote(name)}.log 2>&1")
    print("rc=$?")
    print(verdict_line(name))
