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
from pathlib import Path

ROS2_HOME = "/data/local/tmp/ros2"
TIMEOUT = "180"

def parse_args(argv):
    """Return the optional exact CTest selector without broadening execution.

    Board test callers normally replay every CTest entry in one package.  A
    baseline/exemption gate needs a narrower, auditable mode: it may select one
    safe CTest name, but it must never silently fall back to all tests when the
    name is misspelled.
    """
    if len(argv) == 4:
        return argv[1], argv[2], argv[3], None
    if len(argv) == 6 and argv[4] == "--only-test":
        only_test = argv[5]
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", only_test):
            raise ValueError("--only-test must be a safe CTest name")
        return argv[1], argv[2], argv[3], only_test
    raise ValueError(
        "usage: _parse_ctest_env.py <CTestTestfile.cmake> <package> <workspace> "
        "[--only-test <exact-ctest-name>]"
    )


try:
    ctest_file, pkg, ws_root, only_test = parse_args(sys.argv)
except ValueError as exc:
    print(f"ERROR: {exc}", file=sys.stderr)
    sys.exit(2)
ws_root = ws_root.replace("\\", "/")
build_base = f"{ws_root}/build_ohos"
pkg_build = f"{build_base}/{pkg}"
board_dir = f"{ROS2_HOME}/tests/{pkg}"


def remap(v: str) -> str:
    v = v.replace("\\", "/")
    v = re.sub(re.escape(build_base) + r"/([^/;:\"]+)",
               ROS2_HOME + r"/tests/\1", v)
    v = v.replace(f"{ws_root}/install_ohos", ROS2_HOME)
    # Strip only standalone/path-list host paths.  The former unanchored
    # expression also matched the tail of protocol-like option values such as
    # ``--gtest_output=xml:C:/...`` and could turn ``xml`` into ``xm``.
    v = re.sub(r"(^|[=;])([A-Za-z]:/[^;\"']*)", r"\1", v)
    return v


def remap_arg(value: str, test_name: str) -> str:
    """Remap one argv element without corrupting GTest's ``xml:PATH`` URI."""
    normalized = value.replace("\\", "/")
    if normalized.startswith("--gtest_output=xml:"):
        return f"--gtest_output=xml:{board_dir}/{test_name}.xml"
    build_prefix = f"{pkg_build}/"
    if normalized.startswith(build_prefix):
        relative = normalized[len(build_prefix):]
        if not relative or any(
            not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", component)
            for component in relative.split("/")
        ):
            raise ValueError(
                f"unsafe package-build executable argument for {test_name}: {value}"
            )
        return f"./{relative}"
    return remap(normalized)


def header():
    print("#!/bin/sh")
    # A sourced script may `return 70`; without an explicit guard /bin/sh
    # continues into the test payload with an inherited/stale overlay.
    print(f". {ROS2_HOME}/env.sh || exit 70")
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
    print("overall_rc=0")
    print("mkdir -p .boardtest-verdicts || exit 70")


def emit_verdict(name: str, status: str) -> None:
    """Emit one canonical verdict and a byte-identical archived record."""
    if status == "SKIP":
        print(f"line='BOARDTEST {name} SKIP'")
    else:
        print(
            f"if [ \"$rc\" -eq 0 ]; then line='BOARDTEST {name} PASS rc=0'; "
            f"else line=\"BOARDTEST {name} FAIL rc=$rc\"; overall_rc=1; fi"
        )
    verdict_path = shlex.quote(f".boardtest-verdicts/{name}")
    print(f"printf '%s\\n' \"$line\" > {verdict_path} || exit 70")
    print("printf '%s\\n' \"$line\"")


def ctest_documents(entry):
    """Read only bounded, regular CTest files below this package build root."""
    root = Path(entry).absolute().parent.resolve()
    visited = set()
    documents = []
    total = 0

    def visit(path, depth):
        nonlocal total
        if depth > 32 or len(visited) >= 256:
            raise ValueError("CTest directory traversal exceeds its bound")
        resolved = path.resolve()
        if not resolved.is_relative_to(root) or path.is_symlink() or not path.is_file():
            raise ValueError(f"missing, linked or outside-package CTest file: {path}")
        if resolved in visited:
            raise ValueError(f"repeated CTest directory: {path}")
        visited.add(resolved)
        size = path.stat().st_size
        total += size
        if size > 4 * 1024 * 1024 or total > 16 * 1024 * 1024:
            raise ValueError("CTest input exceeds its byte bound")
        text = path.read_text(encoding="utf-8")
        documents.append(text)
        for match in re.finditer(r"(?m)^\s*subdirs\((.*?)\)\s*$", text):
            directories = shlex.split(match.group(1))
            if not directories:
                raise ValueError("empty CTest subdirectory declaration")
            for directory in directories:
                components = directory.replace("\\", "/").split("/")
                if any(not re.fullmatch(r"[A-Za-z0-9_.-]+", c) for c in components):
                    raise ValueError(f"unsafe CTest subdirectory: {directory}")
                child = path.parent
                for component in components:
                    child /= component
                    if child.is_symlink() or not child.resolve().is_relative_to(root):
                        raise ValueError(f"linked or outside-package CTest subdirectory: {child}")
                visit(child / "CTestTestfile.cmake", depth + 1)

    visit(Path(entry).absolute(), 0)
    return documents


def find_tests(text):
    tests = list(re.finditer(r"add_test\(\[=\[(.+?)\]=\]\s*(.+?)\)\r?\n", text, re.S))
    tests += [m for m in re.finditer(
        r"add_test\(NAME\s+(\S+)\s+COMMAND\s+(.+?)\)\r?\n", text, re.S)
        if not any(t.group(1) == m.group(1) for t in tests)]
    tests += [m for m in re.finditer(
        r'add_test\((?:\[=\[)?"?([\w.-]+)"?(?:\]=\])?\s+(.+?)\)\r?\n', text, re.S)
        if not any(t.group(1) == m.group(1) for t in tests)]
    return tests


try:
    tests = [test for document in ctest_documents(ctest_file) for test in find_tests(document)]
    names = [test.group(1) for test in tests]
    if len(names) != len(set(names)):
        raise ValueError("duplicate CTest names cannot have unique board artifacts")
except (OSError, UnicodeError, ValueError) as exc:
    print(f"ERROR: {exc}", file=sys.stderr)
    sys.exit(2)
seen = set()

if only_test is not None and not any(m.group(1) == only_test for m in tests):
    print(
        f"ERROR: requested CTest selector is absent from {ctest_file}: {only_test}",
        file=sys.stderr,
    )
    sys.exit(2)

header()
for m in tests:
    name, args_blob = m.group(1), m.group(2)
    if name in seen:
        continue
    seen.add(name)
    if only_test is not None and name != only_test:
        continue
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", name):
        print(f"ERROR: unsafe CTest name cannot be archived: {name}", file=sys.stderr)
        sys.exit(2)
    print(f"# BOARDTEST_EXPECTED {name}")
    try:
        args = shlex.split(args_blob.replace('"[=[', '"').replace(']=]"', '"'))
    except ValueError:
        continue
    if "--skip-test" in args:
        emit_verdict(name, "SKIP")
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
        emit_verdict(name, "SKIP")
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
    if any(a.replace("\\", "/").startswith("--gtest_output=xml:") for a in cmd_args):
        print(f"# BOARDTEST_XML {name}")
    try:
        extra = " ".join(shlex.quote(remap_arg(a, name)) for a in cmd_args)
    except ValueError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        sys.exit(2)
    # GTEST_BRIEF via env, not argv: tests with required positional args
    # (test_communication's message type) reject the extra argv element
    env_part = f" {env_prefix}" if env_prefix else ""
    executable = f'./{shlex.quote(exe)}'
    cmd = (f"env GTEST_BRIEF=1{env_part} timeout {TIMEOUT} {executable}"
           f"{' ' + extra if extra else ''}")
    print(f"{cmd} > {shlex.quote(name)}.log 2>&1")
    print("rc=$?")
    emit_verdict(name, "RESULT")

print('exit "$overall_rc"')
