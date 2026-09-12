#!/usr/bin/env python3
"""Acquire, verify, and pin the F106 WSL2/Linux build host and toolchain.

Plan Revision v6 configures and builds the pinned OpenJDK ``jdk21u`` tree on a
genuine Linux host (WSL2), the POSIX layer upstream OpenJDK supports and the
only configuration that can produce a non-Windows target from a Windows
machine.  The host toolchain is not version-pinnable, so this script is the
Feature's own acquisition precondition: it verifies the distribution, installs
and records the approved minimal host package set, acquires and hashes the
**Linux** OpenHarmony SDK (native + toolchains), the Linux OHOS clang, the
Linux toolchain file and target libc, and a Linux x86_64 JDK 21 boot JDK,
creates the Linux-side build tree at ``SOURCE_COMMIT``, and writes the additive
Feature-owned lock that ``TEST-F106-04`` gates.

Nothing is invented.  No hash, URL, version, apiVersion or availability claim is
written unless it was observed at acquire time; every command, probed URL and
index response is recorded in the machine-readable receipt.  The receipt is
written on the **failure** path too (M5/C12), with an explicit non-finding.

The U1 stop rule is a hard stop: if no Linux OHOS SDK exists at API 23 /
version 6.1.0.32, or its clang does not self-report
``OHOS clang 15.0.4 feef13a3...``, the script stops and reports rather than
substituting another SDK, host, or Windows artifact.

Usage::

    python scripts/java/acquire_wsl_toolchain.py --distro Ubuntu-20.04
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import re
import subprocess
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any


LOCK_SCHEMA = "mdds.wsl-toolchain-lock/v1"
RECEIPT_SCHEMA = "mdds.wsl-toolchain-acquire-receipt/v1"

SOURCE_URL = "https://github.com/openjdk/jdk21u.git"
SOURCE_COMMIT = "9de4f68c88a0a1510373f291d1a95b1f6b0db8c8"
SOURCE_TAG = "jdk-21.0.12+8"
OVERLAY_PATHS = ("scripts/", "f106.source.lock.json", "f106.build-profile.json", "f106.license.lock.json")
PINNED_SURFACE = ("src", "make", "doc", "test", "bin", "LICENSE", "ADDITIONAL_LICENSE_INFO", "ASSEMBLY_EXCEPTION")

SDK_API = "23"
SDK_VERSION = "6.1.0.32"
CLANG_REVISION = "feef13a36e78b7a2ff3e9e3f180a958f2782be1e"
CLANG_VERSION = f"OHOS clang 15.0.4 {CLANG_REVISION}"
TOOLCHAIN_FILE_SHA256 = "0CE9943DF04C192725B41CD70DBB259EDD21FA9FA8D78DEEE0D6254F4D6FEB18"
TARGET_LIBC_SHA256 = "298FB33338D06F07552606FF1EF2ADBE42202E4CED3AB9400F26FE90BC38887C"
TARGET_TRIPLE = "aarch64-linux-ohos"
BUILD_TRIPLET_OVERRIDE = "x86_64-unknown-linux-gnu"

BOOT_JDK_URL = (
    "https://github.com/adoptium/temurin21-binaries/releases/download/"
    "jdk-21.0.12%2B8/OpenJDK21U-jdk_x64_linux_hotspot_21.0.12_8.tar.gz"
)
BOOT_JDK_FILE = "OpenJDK21U-jdk_x64_linux_hotspot_21.0.12_8.tar.gz"
BOOT_JDK_DISTRIBUTION = "Temurin/OpenJDK"

# Candidate indexes that are *probed and recorded*, never asserted.  The receipt
# carries each HTTP response so the U1 question ("does a Linux OHOS SDK exist at
# API 23 / 6.1.0.32?") is answered by evidence rather than by a made-up URL.
SDK_INDEX_CANDIDATES = (
    "https://repo.huaweicloud.com/openharmony/os/6.1.0-Release/",
    "https://repo.huaweicloud.com/harmonyos/os/6.1.0-Release/",
)

HOST_TOOLS = ("make", "autoconf", "python3", "cc", "c++", "zip", "unzip", "file")
HOST_TOOL_COMMANDS = {
    "make": ["make", "--version"],
    "autoconf": ["autoconf", "--version"],
    "python3": ["python3", "--version"],
    "cc": ["cc", "--version"],
    "c++": ["c++", "--version"],
    "zip": ["zip", "-v"],
    "unzip": ["unzip", "-v"],
    "file": ["file", "--version"],
}
# The approved minimal package set; F106 installs only from the distribution's
# existing repositories and records the versions it added.
HOST_PACKAGES = ("make", "autoconf", "zip", "unzip", "file", "gcc", "g++", "python3", "git")
MINIMUM_MAKE_VERSION = (4, 0)

WSL_LOCK_CONTENT_FIELD = "content_sha256"
WSL_LOCK_EXCLUDED_KEYS = ["content_sha256", "acquired_at_utc"]
WSL_LOCK_CANONICALIZATION = "utf-8 json; sort_keys=True; separators=(',', ':'); ensure_ascii=True"
WSL_LOCK_HASH_CONTRACT = {
    "algorithm": "sha256",
    "canonicalization": WSL_LOCK_CANONICALIZATION,
    "excluded_keys": WSL_LOCK_EXCLUDED_KEYS,
}

DEFAULT_RECEIPT_REL = "out/f106-wsl-toolchain/receipts/acquire.json"
HEX64 = re.compile(r"^[0-9a-fA-F]{64}$")
POLICY_MARKERS = (
    "permission denied",
    "operation not permitted",
    "access is denied",
    "blocked by policy",
    "application control policy",
    "connection refused",
    "connection reset",
    "network is unreachable",
    "temporary failure in name resolution",
    "name or service not known",
    "no such host",
    "certificate verify failed",
    "timed out",
)
# The policy verdict is closed-set membership on an exact token (AD-7 / M2): the
# marker must occupy a whole, whitespace-stripped, case-folded line.  Word-boundary
# matching is deliberately *not* admissible here -- `AGENTS.md` section 7 makes
# `INFRASTRUCTURE_PERMISSION_FAILURE` never a test failure and never counted
# against the worker failure budget, so a benign line carrying a marker as a word
# would silently remove a real failure from that budget (the `CODE-009` class).
POLICY_MARKER_SET = frozenset(marker.casefold() for marker in POLICY_MARKERS)


class AcquisitionError(RuntimeError):
    """The WSL2/Linux build host could not be acquired or verified."""

    classification = "FAIL"
    failure_kind = "ACQUISITION_INCOMPLETE"
    # The exact closed-set token that produced a policy verdict, or ``None``.
    # Recorded as ``matched_token`` so a receipt shows *why* a classification was
    # made rather than only that it was made (AD-7).
    matched_token: str | None = None

    def __init__(self, *args: Any, matched_token: str | None = None) -> None:
        super().__init__(*args)
        self.matched_token = matched_token


class InfrastructurePermissionError(AcquisitionError):
    """WDAC, sandbox, or network policy blocked a required acquisition step."""

    classification = "INFRASTRUCTURE_PERMISSION_FAILURE"
    failure_kind = "INFRASTRUCTURE_PERMISSION_FAILURE"


class ToolchainIdentityError(AcquisitionError):
    """The Linux OHOS toolchain is absent or is not the pinned one (U1 stop rule)."""

    classification = "FAIL"
    failure_kind = "TOOLCHAIN_IDENTITY_MISMATCH"


# --------------------------------------------------------------------------- #
# pure seams
# --------------------------------------------------------------------------- #


def lock_content_sha256(lock: dict[str, Any]) -> str:
    """SHA-256 over the lock's stable fields, honouring ``hash_contract``.

    Canonical form: every key except those named by
    ``hash_contract.excluded_keys``, serialized as UTF-8 JSON with sorted keys
    and no insignificant whitespace.  ``acquired_at_utc`` is excluded, so
    re-running the acquisition with only a new timestamp yields the *same*
    content hash (the v5 volatility defect, fixed and positively tested).
    """
    excluded_keys = WSL_LOCK_EXCLUDED_KEYS
    contract = lock.get("hash_contract")
    if isinstance(contract, dict) and isinstance(contract.get("excluded_keys"), list):
        excluded_keys = [str(key) for key in contract["excluded_keys"]]
    excluded = set(excluded_keys)
    payload = {key: value for key, value in lock.items() if key not in excluded}
    encoded = json.dumps(payload, sort_keys=True, separators=(",", ":"), ensure_ascii=True)
    return hashlib.sha256(encoded.encode("utf-8")).hexdigest().upper()


def boot_jdk_accepts(record: object) -> bool:
    """Pure Linux x86_64 boot-JDK predicate; a Windows artifact fails closed."""
    if not isinstance(record, dict):
        return False
    if str(record.get("os", "")).strip().lower() != "linux":
        return False
    if str(record.get("arch", "")).strip().lower() not in ("x86_64", "amd64"):
        return False
    if not str(record.get("java_version", "")).strip():
        return False
    if "21.0.12" not in str(record.get("java_version", "")):
        return False
    if not HEX64.match(str(record.get("sha256", "")).upper()):
        return False
    blob = f"{record.get('file', '')} {record.get('url', '')}".lower()
    if "windows" in blob or ".exe" in blob or "win64" in blob:
        return False
    return True


# --------------------------------------------------------------------------- #
# WSL plumbing
# --------------------------------------------------------------------------- #

# Script text is never passed to wsl.exe as a command-line word.  WSL's interop
# layer re-runs that command line through a shell *before* the intended inner
# ``bash -lc`` parses it, so every ``$VAR`` in an argv word is substituted
# against the outer environment first: in the F106 U1 run ``$root`` became the
# empty string (``mkdir: cannot create directory ''``) and ``${Package}`` in the
# package inventory silently vanished.  Quoting and backslash escaping do not
# help -- the outer parse consumes the backslash as well.  The script therefore
# travels base64-encoded on **stdin**, where no host-side parse layer can reach
# it, and ``wsl_script_invocation`` is the single constructor of that argv.
SCRIPT_TRANSPORT = "base64-stdin"
SCRIPT_DECODER_PIPELINE = "base64 -d | bash -l"


def wsl_script_invocation(distro: str, script: str) -> tuple[list[str], str]:
    """Return the ``(argv, stdin)`` pair that runs ``script`` inside ``distro``.

    ``argv`` is exactly what is handed to ``CreateProcess``; ``stdin`` is the
    base64 encoding of ``script``, which the host decodes and executes as
    ``base64 -d | bash -l``.  Because the body is stdin data and not a
    command-line word, no host-side shell can expand, re-quote or truncate it.

    The inner shell is still a login shell reading the script from stdin, so
    ``set -e``, pipelines, heredocs and the script's own exit status are
    unchanged.  The one behavioural difference is that the script body no
    longer sees the caller's stdin -- it *is* the caller's stdin.
    """
    encoded = base64.b64encode(script.encode("utf-8")).decode("ascii")
    # ``subprocess`` in text mode translates "\n" to os.linesep on the way to
    # the child's stdin, and ``Popen`` exposes no ``newline=`` knob to stop it,
    # so a multi-line payload would arrive CRLF-mangled on the Linux side (that
    # is what turned ``set -e`` into ``set: -`` in the F106 stdin-staging
    # probe).  ``b64encode`` never wraps, so the payload is a single line and
    # there is nothing to translate; this guard keeps it that way.  Its
    # lookalike ``base64.encodebytes`` wraps at 76 columns and would silently
    # reintroduce the defect.
    if "\n" in encoded or "\r" in encoded:
        raise AcquisitionError(
            "internal error: the base64 script payload must be a single line, otherwise Windows "
            "newline translation mangles it on the way into the build host"
        )
    return ["wsl.exe", "-d", distro, "--", "bash", "-lc", SCRIPT_DECODER_PIPELINE], encoded


class Recorder:
    """Runs build-host commands and records every one of them for the receipt."""

    def __init__(self) -> None:
        self.commands: list[dict[str, Any]] = []
        self.probes: dict[str, Any] = {}

    def run(self, distro: str, script: str, timeout: int = 600, kind: str = "script") -> subprocess.CompletedProcess[str]:
        command, payload = wsl_script_invocation(distro, script)
        try:
            completed = subprocess.run(
                command,
                input=payload,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=timeout,
                check=False,
            )
        except FileNotFoundError as exc:
            raise InfrastructurePermissionError(f"wsl.exe is unavailable: {exc}") from exc
        except PermissionError as exc:
            raise InfrastructurePermissionError(f"host policy blocked wsl.exe: {exc}") from exc
        except subprocess.TimeoutExpired as exc:
            raise InfrastructurePermissionError(f"wsl.exe timed out after {timeout}s") from exc
        except OSError as exc:
            raise InfrastructurePermissionError(f"wsl.exe could not be started: {exc}") from exc
        self.commands.append(
            {
                "kind": kind,
                "command": command,
                "script_transport": SCRIPT_TRANSPORT,
                "script": script,
                "exit_code": completed.returncode,
                "output": (completed.stdout or "")[-20000:],
            }
        )
        return completed

    def probe(self, name: str, value: object) -> None:
        self.probes[name] = value


def _wsl_list_verbose(recorder: Recorder) -> str:
    try:
        completed = subprocess.run(
            ["wsl.exe", "-l", "-v"],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=60,
            check=False,
        )
    except PermissionError as exc:
        raise InfrastructurePermissionError(f"host policy blocked wsl.exe: {exc}") from exc
    except subprocess.TimeoutExpired as exc:
        raise InfrastructurePermissionError("wsl.exe -l -v timed out") from exc
    except OSError as exc:
        raise InfrastructurePermissionError(f"wsl.exe could not be started: {exc}") from exc
    raw = completed.stdout or b""
    text = raw.decode("utf-8", errors="replace")
    for encoding in ("utf-16-le", "utf-8"):
        try:
            candidate = raw.decode(encoding).replace("\x00", "")
        except UnicodeDecodeError:
            continue
        if "NAME" in candidate.upper() or "Ubuntu" in candidate:
            text = candidate
            break
    recorder.commands.append(
        {"kind": "wsl-list", "command": ["wsl.exe", "-l", "-v"], "exit_code": completed.returncode, "output": text}
    )
    return text


def _distribution_row(listing: str, distro: str) -> dict[str, Any] | None:
    for line in listing.splitlines():
        stripped = line.strip()
        if not stripped or stripped.upper().startswith("NAME"):
            continue
        tokens = [token for token in stripped.split() if token != "*"]
        if tokens and tokens[0] == distro:
            version = tokens[-1] if len(tokens) >= 3 else ""
            return {"name": tokens[0], "state": tokens[1] if len(tokens) >= 2 else "", "version": version}
    return None


def _default_distribution(listing: str) -> str:
    for line in listing.splitlines():
        stripped = line.strip()
        if not stripped or stripped.upper().startswith("NAME"):
            continue
        tokens = [token for token in stripped.split() if token != "*"]
        if tokens:
            return tokens[0]
    raise AcquisitionError("no live WSL distribution is available for the F106 build host (U8)")


def _line(recorder: Recorder, distro: str, script: str, timeout: int = 120) -> str:
    completed = recorder.run(distro, script, timeout=timeout, kind="probe")
    if completed.returncode != 0:
        raise AcquisitionError(f"the build host refused {script!r} (exit {completed.returncode}):\n{completed.stdout}")
    lines = [item.strip() for item in (completed.stdout or "").splitlines() if item.strip()]
    return lines[-1] if lines else ""


def _python3(recorder: Recorder, distro: str, code: str, timeout: int = 900, kind: str = "python") -> str:
    """Run a Python fragment inside the build host via a heredoc (no quoting hazards)."""
    script = "python3 - <<'MDDS_F106_PY'\n" + code + "\nMDDS_F106_PY\n"
    completed = recorder.run(distro, script, timeout=timeout, kind=kind)
    if completed.returncode != 0:
        raise AcquisitionError(f"the build host python3 fragment failed (exit {completed.returncode}):\n{completed.stdout}")
    return (completed.stdout or "").strip()


def _sha256_on_host(recorder: Recorder, distro: str, path: str) -> str:
    completed = recorder.run(distro, f"sha256sum {json.dumps(path)}", timeout=1800, kind="hash")
    if completed.returncode != 0:
        raise AcquisitionError(f"cannot hash {path} on the build host:\n{completed.stdout}")
    fields = (completed.stdout or "").split()
    digest = fields[0].upper() if fields else ""
    if not HEX64.match(digest):
        raise AcquisitionError(f"the build host returned no well-formed digest for {path}: {completed.stdout!r}")
    return digest


def _policy_matched_token(detail: str) -> str | None:
    """The ``POLICY_MARKERS`` member ``detail`` states, or ``None``.

    AD-7 / M2 states the rule by hazard shape, not by matching form.  The hazard
    at this site is *a marker appearing as a word inside benign text*, so the
    admissible form is a whole-line (field-exact) closed-set membership: a line
    of ``detail`` -- stripped of surrounding whitespace and case-folded -- must be
    **equal** to a member of ``POLICY_MARKER_SET``.  Word-boundary matching is not
    admissible here: an ``INFRASTRUCTURE_PERMISSION_FAILURE`` is never counted
    against the worker failure budget (``AGENTS.md`` section 7), so a benign log
    line carrying a marker as a word would report a real failure as a permission
    problem and remove it from that budget.

    Fail-closed: anything that is not an exact member of the closed set yields
    ``None``.  The matched token is returned so the caller can record it as
    ``matched_token``.
    """
    for line in detail.splitlines():
        token = line.strip().casefold()
        if token in POLICY_MARKER_SET:
            return token
    return None


def _is_policy_failure(detail: str) -> bool:
    """``True`` iff ``detail`` carries a policy verdict (see :func:`_policy_matched_token`)."""
    return _policy_matched_token(detail) is not None


# --------------------------------------------------------------------------- #
# acquisition steps
# --------------------------------------------------------------------------- #


def _verify_distribution(recorder: Recorder, distro: str) -> dict[str, Any]:
    listing = _wsl_list_verbose(recorder)
    recorder.probe("wsl_list_verbose", listing)
    row = _distribution_row(listing, distro)
    recorder.probe("distribution_registered", row is not None)
    if row is None:
        raise AcquisitionError(
            f"the WSL distribution {distro!r} is not registered on this host (U8):\n{listing}"
        )
    try:
        version = int(row["version"])
    except (TypeError, ValueError) as exc:
        raise AcquisitionError(f"the WSL distribution {distro!r} reports an unreadable version: {row!r}") from exc
    if version != 2:
        raise AcquisitionError(f"F106 requires a WSL2 distribution; {distro!r} is version {version}")

    uname_s = _line(recorder, distro, "uname -s")
    uname_r = _line(recorder, distro, "uname -r")
    # Independent in-shell probe: the OS the shell actually runs on, probed
    # separately from any value we will later declare.
    kernel_ostype = _line(recorder, distro, "cat /proc/sys/kernel/ostype")
    if uname_s != "Linux" or kernel_ostype != "Linux":
        raise AcquisitionError(
            f"the F106 build host must be Linux; uname -s = {uname_s!r}, /proc/sys/kernel/ostype = {kernel_ostype!r}"
        )
    recorder.probe("uname_s", uname_s)
    recorder.probe("uname_r", uname_r)
    recorder.probe("kernel_ostype", kernel_ostype)
    return {
        "distribution": distro,
        "wsl_version": version,
        "state": row.get("state", ""),
        "uname_s": uname_s,
        "uname_r": uname_r,
        "actual_host_os": "linux",
        "actual_host_os_probe": "/proc/sys/kernel/ostype",
    }


def _probe_workspace(recorder: Recorder, distro: str, root: str) -> dict[str, Any]:
    script = f"""
set -e
root={json.dumps(root)}
mkdir -p "$root"
rm -rf "$root/.f106-probe"
mkdir -p "$root/.f106-probe"
ln -s f106-target "$root/.f106-probe/link"
[ "$(readlink "$root/.f106-probe/link")" = "f106-target" ] && echo SYMLINK_OK || echo SYMLINK_FAIL
printf '#!/bin/sh\\necho F106_EXEC_OK\\n' > "$root/.f106-probe/run.sh"
chmod +x "$root/.f106-probe/run.sh"
out=$("$root/.f106-probe/run.sh" 2>/dev/null || true)
[ "$out" = "F106_EXEC_OK" ] && echo EXEC_OK || echo EXEC_FAIL
rm -rf "$root/.f106-probe"
[ -e "$root/.f106-probe" ] && echo DELETE_FAIL || echo DELETE_OK
case "$root" in /mnt/*) echo ON_MNT ;; *) echo NOT_ON_MNT ;; esac
"""
    completed = recorder.run(distro, script, timeout=300, kind="workspace-probe")
    output = completed.stdout or ""
    # The probe's verdicts are whole lines.  They must be matched as whole
    # tokens: a substring test makes the negative token the affirmative one,
    # because "NOT_ON_MNT" contains "ON_MNT" -- which reported every workspace
    # as living under /mnt and failed the probe on a correct host.
    tokens = {line.strip() for line in output.splitlines()}
    symlink = "PASS" if "SYMLINK_OK" in tokens else "FAIL"
    write_exec = "PASS" if {"EXEC_OK", "DELETE_OK"} <= tokens else "FAIL"
    on_mnt = "ON_MNT" in tokens
    if symlink != "PASS" or write_exec != "PASS" or on_mnt:
        raise AcquisitionError(
            f"the Linux workspace {root!r} failed its probe (symlink={symlink} write/exec/delete={write_exec} "
            f"on_mnt={on_mnt}):\n{output}"
        )
    return {"symlink_round_trip": symlink, "write_exec_delete": write_exec, "on_mnt": on_mnt}


def _package_inventory(recorder: Recorder, distro: str) -> dict[str, str]:
    completed = recorder.run(
        distro, "dpkg-query -W -f='${Package} ${Version}\\n' 2>/dev/null | sort", timeout=300, kind="inventory"
    )
    inventory: dict[str, str] = {}
    for line in (completed.stdout or "").splitlines():
        fields = line.split()
        if len(fields) >= 2:
            inventory[fields[0]] = fields[1]
    return inventory


def _install_packages(recorder: Recorder, distro: str) -> None:
    completed = recorder.run(distro, "id -u", timeout=60, kind="probe")
    is_root = (completed.stdout or "").strip() == "0"
    prefix = "" if is_root else "sudo -n "
    if not is_root:
        sudo_probe = recorder.run(distro, "sudo -n true", timeout=60, kind="probe")
        if sudo_probe.returncode != 0:
            raise InfrastructurePermissionError(
                "the build host does not permit non-interactive package installation "
                f"(sudo -n failed):\n{sudo_probe.stdout}"
            )
    install = (
        f"{prefix}env DEBIAN_FRONTEND=noninteractive apt-get -o Acquire::Retries=3 update && "
        f"{prefix}env DEBIAN_FRONTEND=noninteractive apt-get -y install {' '.join(HOST_PACKAGES)}"
    )
    completed = recorder.run(distro, install, timeout=3600, kind="install")
    if completed.returncode != 0:
        detail = completed.stdout or ""
        matched_token = _policy_matched_token(detail)
        if matched_token is not None:
            raise InfrastructurePermissionError(
                f"package installation was blocked by host policy "
                f"(matched_token={matched_token!r}):\n{detail}",
                matched_token=matched_token,
            )
        raise AcquisitionError(f"package installation failed (exit {completed.returncode}):\n{detail}")


def _version_tuple(text: object) -> tuple[int, ...]:
    match = re.search(r"(\d+(?:\.\d+)*)", str(text))
    if not match:
        raise AcquisitionError(f"unparseable version string: {text!r}")
    return tuple(int(part) for part in match.group(1).split("."))


def _verify_host_tools(recorder: Recorder, distro: str) -> dict[str, Any]:
    records: dict[str, Any] = {}
    for tool in HOST_TOOLS:
        command = HOST_TOOL_COMMANDS[tool]
        completed = recorder.run(distro, " ".join(command), timeout=180, kind="host-tool")
        output = (completed.stdout or "").strip()
        path = _line(recorder, distro, f"command -v {tool}", timeout=60)
        if not path.startswith("/"):
            raise AcquisitionError(f"host tool {tool!r} does not resolve to an absolute POSIX path: {path!r}")
        if completed.returncode != 0 or not output:
            raise AcquisitionError(f"host tool {tool!r} failed its verification (exit {completed.returncode}):\n{output}")
        record: dict[str, Any] = {
            "result": "PASS",
            "path": path,
            "command": command,
            "cwd": "/",
            "output": output.splitlines()[0] if output else "",
            "exit_code": completed.returncode,
        }
        if tool in ("make", "autoconf", "python3"):
            record["version"] = ".".join(str(part) for part in _version_tuple(output))
        records[tool] = record
    make_version = _version_tuple(records["make"]["version"])
    if make_version < MINIMUM_MAKE_VERSION:
        raise AcquisitionError(f"host make is older than the required 4.0: {records['make']['version']!r}")
    return records


def _probe_index(recorder: Recorder, distro: str, url: str) -> dict[str, Any]:
    code = f"""
import json, urllib.request, urllib.error
url = {url!r}
try:
    with urllib.request.urlopen(url, timeout=60) as response:
        body = response.read(400000).decode('utf-8', 'replace')
        print(json.dumps({{'url': url, 'status': getattr(response, 'status', 200), 'body_length': len(body), 'body': body}}))
except urllib.error.HTTPError as exc:
    print(json.dumps({{'url': url, 'status': exc.code, 'error': str(exc)}}))
except Exception as exc:
    print(json.dumps({{'url': url, 'error': str(exc)}}))
"""
    raw = _python3(recorder, distro, code, timeout=300, kind="index-probe")
    try:
        return json.loads(raw.splitlines()[-1])
    except (json.JSONDecodeError, IndexError):
        return {"url": url, "error": f"unparseable index response: {raw[:400]!r}"}


def _download(recorder: Recorder, distro: str, url: str, destination: str) -> str:
    code = f"""
import hashlib, json, os, urllib.request
url = {url!r}
destination = {destination!r}
os.makedirs(os.path.dirname(destination), exist_ok=True)
digest = hashlib.sha256()
size = 0
try:
    with urllib.request.urlopen(url, timeout=600) as response, open(destination, 'wb') as stream:
        while True:
            block = response.read(1024 * 1024)
            if not block:
                break
            digest.update(block)
            size += len(block)
            stream.write(block)
    print(json.dumps({{'url': url, 'path': destination, 'size': size, 'sha256': digest.hexdigest().upper()}}))
except Exception as exc:
    print(json.dumps({{'url': url, 'error': str(exc)}}))
"""
    raw = _python3(recorder, distro, code, timeout=7200, kind="download")
    try:
        result = json.loads(raw.splitlines()[-1])
    except (json.JSONDecodeError, IndexError) as exc:
        raise InfrastructurePermissionError(f"could not download {url}: unreadable response {raw[:400]!r}") from exc
    if "error" in result:
        detail = str(result["error"])
        matched_token = _policy_matched_token(detail)
        if matched_token is not None:
            raise InfrastructurePermissionError(
                f"the download of {url} was blocked by host policy "
                f"(matched_token={matched_token!r}): {detail}",
                matched_token=matched_token,
            )
        raise AcquisitionError(f"the download of {url} failed: {detail}")
    if int(result.get("size", 0)) <= 0:
        raise AcquisitionError(f"the download of {url} produced an empty file")
    return str(result["sha256"]).upper()


def _resolve_sdk_urls(recorder: Recorder, distro: str, args: argparse.Namespace) -> dict[str, str]:
    """Locate the Linux SDK packages by probing candidate indexes, or use overrides."""
    if args.sdk_native_url and args.sdk_toolchains_url:
        recorder.probe("sdk_url_source", "explicit override")
        return {"native": args.sdk_native_url, "toolchains": args.sdk_toolchains_url}

    responses = []
    for index in SDK_INDEX_CANDIDATES:
        response = _probe_index(recorder, distro, index)
        responses.append(response)
        body = str(response.get("body", ""))
        native = re.search(r'href="([^"]*(?:native|ohos-sdk-native)[^"]*linux[^"]*\.(?:zip|tar\.gz))"', body, re.IGNORECASE)
        toolchains = re.search(r'href="([^"]*toolchains[^"]*linux[^"]*\.(?:zip|tar\.gz))"', body, re.IGNORECASE)
        if native and toolchains:
            base = index if index.endswith("/") else index + "/"
            recorder.probe("sdk_index_resolved", index)
            return {"native": base + native.group(1), "toolchains": base + toolchains.group(1)}
    recorder.probe("sdk_index_responses", responses)
    raise ToolchainIdentityError(
        "no Linux OpenHarmony SDK package (native + toolchains) could be located for API "
        f"{SDK_API} / version {SDK_VERSION}; the probed indexes and their responses are recorded in the receipt. "
        "U1 is unresolved: no substitute SDK, host, or Windows artifact is accepted."
    )


def _extract(recorder: Recorder, distro: str, archive: str, destination: str) -> None:
    script = f"""
set -e
rm -rf {json.dumps(destination)}
mkdir -p {json.dumps(destination)}
case {json.dumps(archive)} in
  *.zip) unzip -q {json.dumps(archive)} -d {json.dumps(destination)} ;;
  *) tar -xzf {json.dumps(archive)} -C {json.dumps(destination)} ;;
esac
"""
    completed = recorder.run(distro, script, timeout=3600, kind="extract")
    if completed.returncode != 0:
        raise AcquisitionError(f"could not extract {archive}:\n{completed.stdout}")


def _find_package_root(recorder: Recorder, distro: str, tree: str) -> dict[str, dict[str, Any]]:
    """Locate and read every ``oh-uni-package.json`` below ``tree``."""
    code = f"""
import json, os
root = {tree!r}
found = {{}}
for base, _dirs, files in os.walk(root):
    if 'oh-uni-package.json' in files:
        path = os.path.join(base, 'oh-uni-package.json')
        try:
            with open(path, encoding='utf-8', errors='replace') as stream:
                found[path] = json.load(stream)
        except Exception as exc:
            found[path] = {{'error': str(exc)}}
print(json.dumps(found))
"""
    raw = _python3(recorder, distro, code, timeout=600, kind="scan")
    try:
        return json.loads(raw.splitlines()[-1])
    except (json.JSONDecodeError, IndexError) as exc:
        raise AcquisitionError(f"could not read the OHOS package identity files under {tree}: {raw[:400]!r}") from exc


def _acquire_sdk(recorder: Recorder, distro: str, args: argparse.Namespace, workspace: str) -> dict[str, Any]:
    urls = _resolve_sdk_urls(recorder, distro, args)
    downloads = f"{workspace}/downloads"
    install = f"{workspace}/sdk"
    packages: dict[str, Any] = {}
    for name in ("native", "toolchains"):
        url = urls[name]
        archive = f"{downloads}/{os.path.basename(url.split('?')[0])}"
        digest = _download(recorder, distro, url, archive)
        size = int(_line(recorder, distro, f"stat -c %s {json.dumps(archive)}"))
        _extract(recorder, distro, archive, f"{install}/{name}")
        packages[name] = {
            "file": os.path.basename(archive),
            "path": archive,
            "url": url,
            "size": size,
            "sha256": digest,
            "download_sha256": digest,
        }

    identities = _find_package_root(recorder, distro, install)
    for name in ("native", "toolchains"):
        match = None
        for path, payload in identities.items():
            if not isinstance(payload, dict):
                continue
            declared = str(payload.get("path", "")) or path
            if f"/{name}/" in path or declared.strip("/").endswith(name):
                match = (path, payload)
                break
        if match is None:
            raise ToolchainIdentityError(
                f"the acquired Linux SDK {name} package does not expose an oh-uni-package.json identity"
            )
        path, payload = match
        api_version = str(payload.get("apiVersion", ""))
        version = str(payload.get("version", ""))
        if api_version != SDK_API or version != SDK_VERSION:
            raise ToolchainIdentityError(
                f"the acquired Linux SDK {name} package is API {api_version!r} / version {version!r}, not "
                f"API {SDK_API} / version {SDK_VERSION} (U1 stop rule)"
            )
        packages[name]["apiVersion"] = api_version
        packages[name]["version"] = version
        packages[name]["package_identity_file"] = path

    sdk_root = f"{install}/native"
    clang_path = f"{sdk_root}/llvm/bin/clang"
    cxx_path = f"{sdk_root}/llvm/bin/clang++"
    clang_version = _line(recorder, distro, f"{json.dumps(clang_path)} --version", timeout=300)
    if CLANG_REVISION not in clang_version:
        raise ToolchainIdentityError(
            f"the acquired Linux clang does not self-report the pinned revision {CLANG_REVISION!r}: "
            f"{clang_version!r} (U1 stop rule; TOOLCHAIN_IDENTITY_MISMATCH)"
        )
    for tool in (clang_path, cxx_path):
        present = _line(recorder, distro, f"test -x {json.dumps(tool)} && echo OK")
        if present != "OK":
            raise ToolchainIdentityError(f"the acquired Linux OHOS SDK is missing {tool}")

    target_artifacts = {}
    for label, relative, expected in (
        ("toolchain_file", "build/cmake/ohos.toolchain.cmake", TOOLCHAIN_FILE_SHA256),
        ("target_libc", f"sysroot/usr/lib/{TARGET_TRIPLE}/libc.so", TARGET_LIBC_SHA256),
    ):
        path = f"{sdk_root}/{relative}"
        observed = _sha256_on_host(recorder, distro, path)
        target_artifacts[label] = {
            "path": path,
            "expected_sha256": expected,
            "observed_sha256": observed,
            "delta": observed != expected,
        }
    return {
        "root": sdk_root,
        "install_root": install,
        "packages": packages,
        "clang": {
            "path": clang_path,
            "cxx_path": cxx_path,
            "version": clang_version,
            "sha256": _sha256_on_host(recorder, distro, clang_path),
            "cxx_sha256": _sha256_on_host(recorder, distro, cxx_path),
        },
        "target_artifacts": target_artifacts,
        "identity_files": sorted(identities),
    }


def _acquire_boot_jdk(recorder: Recorder, distro: str, args: argparse.Namespace, workspace: str) -> dict[str, Any]:
    url = args.boot_jdk_url or BOOT_JDK_URL
    archive = f"{workspace}/downloads/{os.path.basename(url.split('?')[0])}"
    digest = _download(recorder, distro, url, archive)
    size = int(_line(recorder, distro, f"stat -c %s {json.dumps(archive)}"))
    root_dir = f"{workspace}/toolchains/boot-jdk"
    completed = recorder.run(
        distro,
        f"rm -rf {json.dumps(root_dir)} && mkdir -p {json.dumps(root_dir)} && tar -xzf {json.dumps(archive)} -C {json.dumps(root_dir)}",
        timeout=1800,
        kind="extract",
    )
    if completed.returncode != 0:
        raise AcquisitionError(f"could not extract the Linux boot JDK:\n{completed.stdout}")
    java = _line(recorder, distro, f"ls -d {json.dumps(root_dir)}/*/bin/java | head -n 1")
    if not java.endswith("/bin/java"):
        raise AcquisitionError(f"the acquired Linux boot JDK has no bin/java: {java!r}")
    boot_root = java[: -len("/bin/java")]
    java_version = _line(recorder, distro, f"{json.dumps(java)} -version 2>&1 | head -n 1", timeout=300)
    record = {
        "url": url,
        "file": os.path.basename(archive),
        "path": archive,
        "root": boot_root,
        "size": size,
        "sha256": digest,
        "os": "linux",
        "arch": "x86_64",
        "version": SOURCE_TAG,
        "distribution": BOOT_JDK_DISTRIBUTION,
        "java_version": java_version,
    }
    if not boot_jdk_accepts(record):
        raise ToolchainIdentityError(f"the acquired boot JDK is not an accepted Linux x86_64 JDK 21: {record!r}")
    return record


def _prepare_build_tree(
    recorder: Recorder, distro: str, workspace: str, overlay_source: str | None
) -> dict[str, Any]:
    tree = f"{workspace}/src/jdk21u"
    script = f"""
set -e
tree={json.dumps(tree)}
rm -rf "$tree"
mkdir -p "$tree"
git -C "$tree" init -q
git -C "$tree" remote add origin {json.dumps(SOURCE_URL)}
if ! git -C "$tree" fetch -q --depth 1 origin {json.dumps(SOURCE_COMMIT)}; then
  git -C "$tree" fetch -q --depth 1 origin refs/tags/{json.dumps(SOURCE_TAG)}
fi
git -C "$tree" checkout -q FETCH_HEAD
git -C "$tree" rev-parse HEAD
"""
    completed = recorder.run(distro, script, timeout=3600, kind="build-tree")
    if completed.returncode != 0:
        raise AcquisitionError(f"could not create the Linux-side build tree:\n{completed.stdout}")

    if overlay_source:
        overlay = (
            f"mkdir -p {json.dumps(tree)}/scripts && "
            f"cp -a {json.dumps(overlay_source)}/scripts/. {json.dumps(tree)}/scripts/ && "
            + " && ".join(
                f"cp -a {json.dumps(overlay_source)}/{name} {json.dumps(tree)}/{name}"
                for name in OVERLAY_PATHS
                if not name.endswith("/")
            )
        )
        staged = recorder.run(distro, overlay, timeout=1800, kind="overlay")
        if staged.returncode != 0:
            raise AcquisitionError(f"could not materialize the F106 overlay into the build tree:\n{staged.stdout}")

    raw_commands = []
    checks = {
        "head": f"git -C {json.dumps(tree)} rev-parse HEAD",
        "origin": f"git -C {json.dumps(tree)} remote get-url origin",
        "commit_present": f"git -C {json.dumps(tree)} cat-file -e {SOURCE_COMMIT}^{{commit}} && echo OK",
        "tracked_tree_clean": f"git -C {json.dumps(tree)} status --porcelain -uno",
        "pinned_surface_clean": f"git -C {json.dumps(tree)} status --porcelain -uno -- {' '.join(PINNED_SURFACE)}",
        "configure_executable": f"test -x {json.dumps(tree)}/configure && echo OK",
    }
    observed: dict[str, str] = {}
    for label, script_text in checks.items():
        completed = recorder.run(distro, script_text, timeout=300, kind="build-tree-check")
        observed[label] = (completed.stdout or "").strip()
        raw_commands.append(
            {
                "label": label,
                "command": wsl_script_invocation(distro, script_text)[0],
                "script_transport": SCRIPT_TRANSPORT,
                "script": script_text,
                "stdout": (completed.stdout or "").strip(),
                "exit_code": completed.returncode,
            }
        )
    if observed["head"] != SOURCE_COMMIT:
        raise AcquisitionError(f"the Linux build tree HEAD is {observed['head']!r}, expected {SOURCE_COMMIT}")
    if observed["origin"] != SOURCE_URL:
        raise AcquisitionError(f"the Linux build tree origin is {observed['origin']!r}, expected {SOURCE_URL}")
    if "OK" not in observed["commit_present"]:
        raise AcquisitionError(f"the Linux build tree lacks {SOURCE_COMMIT}")
    if observed["tracked_tree_clean"]:
        raise AcquisitionError(f"the Linux build tree tracked surface is dirty:\n{observed['tracked_tree_clean']}")
    if observed["pinned_surface_clean"]:
        raise AcquisitionError(f"the Linux build tree pinned surface is dirty:\n{observed['pinned_surface_clean']}")
    if "OK" not in observed["configure_executable"]:
        raise AcquisitionError("the Linux build tree has no executable configure script")

    return {
        "path": tree,
        "head": observed["head"],
        "origin": observed["origin"],
        "commit_present": True,
        "tracked_tree_clean": True,
        "pinned_surface_clean": True,
        "configure_executable": True,
        "overlay_materialized": bool(overlay_source),
        "raw_commands": raw_commands,
    }


def _finish(recorder: Recorder, distro: str, workspace_root: str) -> None:
    completed = recorder.run(
        distro,
        f"chmod -R u+rwX {json.dumps(workspace_root)} && find {json.dumps(workspace_root)} -name __pycache__ -type d "
        f"-prune -exec rm -rf {{}} + || true",
        timeout=600,
        kind="finish",
    )
    del completed


# --------------------------------------------------------------------------- #
# driver
# --------------------------------------------------------------------------- #


def _parse_index_version(listing: str, distro: str) -> int:
    row = _distribution_row(listing, distro)
    if row is None:
        raise AcquisitionError(f"the WSL distribution {distro!r} is not registered on this host")
    try:
        return int(row["version"])
    except (TypeError, ValueError) as exc:
        raise AcquisitionError(f"the WSL distribution {distro!r} reports an unreadable version") from exc


def _windows_to_wsl_path(path: str) -> str:
    """Map a Windows drive path to its Linux-side view, arithmetically.

    Used only to bring the workspace-authored overlay *into* the Linux
    filesystem; no path-conversion tool is invoked, and the result is never
    recorded as a build input.
    """
    text = str(path)
    match = re.fullmatch(r"([A-Za-z]):[\\/](.*)", text)
    if not match:
        return text
    return f"/mnt/{match.group(1).lower()}/" + match.group(2).replace("\\", "/")


def acquire(args: argparse.Namespace, recorder: Recorder) -> dict[str, Any]:
    """Run the acquisition and return the lock.  Raises AcquisitionError on any stop."""
    listing = _wsl_list_verbose(recorder)
    recorder.probe("wsl_list_verbose", listing)
    distro = args.distro or _default_distribution(listing)
    recorder.probe("distro", distro)
    recorder.probe("distribution_registered", _distribution_row(listing, distro) is not None)
    _parse_index_version(listing, distro)

    wsl = _verify_distribution(recorder, distro)

    overlay_source = args.overlay_source
    if overlay_source:
        if not Path(overlay_source).is_dir():
            raise AcquisitionError(f"the F106 overlay source is not a directory: {overlay_source}")
        overlay_source = _windows_to_wsl_path(overlay_source)
    args.overlay_source = overlay_source

    home = _line(recorder, distro, 'printf %s "$HOME"')
    workspace_root = args.linux_root or f"{home}/mdds-f106"
    if not workspace_root.startswith("/") or workspace_root.startswith("/mnt/"):
        raise AcquisitionError(f"the Linux workspace root must be a Linux path outside /mnt: {workspace_root!r}")
    workspace_probe = _probe_workspace(recorder, distro, workspace_root)

    pre_install = _package_inventory(recorder, distro)
    _install_packages(recorder, distro)
    post_install = _package_inventory(recorder, distro)
    added = sorted(
        name for name, version in post_install.items() if pre_install.get(name) != version
    )

    # The python3 record is completed after the build tree exists, because the
    # py_compile probe must run against the real producer.
    host_tools = _verify_host_tools(recorder, distro)

    sdk = _acquire_sdk(recorder, distro, args, workspace_root)
    boot_jdk = _acquire_boot_jdk(recorder, distro, args, workspace_root)
    build_tree = _prepare_build_tree(recorder, distro, workspace_root, overlay_source)

    producer_linux = f"{build_tree['path']}/scripts/build_ohos_jre.py"
    compiled = recorder.run(distro, f"python3 -m py_compile {json.dumps(producer_linux)}", timeout=300, kind="py-compile")
    host_tools["python3"]["py_compile_command"] = ["python3", "-m", "py_compile", producer_linux]
    host_tools["python3"]["py_compile_output"] = (compiled.stdout or "").strip() or "py_compile: no diagnostics"
    host_tools["python3"]["py_compile_result"] = "PASS" if compiled.returncode == 0 else "FAIL"
    if compiled.returncode != 0:
        raise AcquisitionError(
            f"the acquired python3 cannot py_compile the F106 producer (exit {compiled.returncode}):\n{compiled.stdout}"
        )

    _finish(recorder, distro, workspace_root)

    lock: dict[str, Any] = {
        "schema": LOCK_SCHEMA,
        "acquired_at_utc": args.started_at_utc,
        "acquisition_receipt": DEFAULT_RECEIPT_REL,
        "hash_contract": dict(WSL_LOCK_HASH_CONTRACT),
        "actual_host_os": "linux",
        "declared_build_os": "linux",
        "effective_build_os": "linux",
        "build_triplet_override": BUILD_TRIPLET_OVERRIDE,
        "path_convention": "posix",
        "wsl": wsl,
        "linux_workspace_root": workspace_root,
        "linux_tree": build_tree["path"],
        "workspace_probe": workspace_probe,
        "host_packages": {"pre_install": sorted(pre_install.items()), "added": added},
        "host_tools": host_tools,
        "sdk": sdk,
        "boot_jdk": boot_jdk,
        "build_tree": build_tree,
        "source": {
            "repository": SOURCE_URL,
            "commit": SOURCE_COMMIT,
            "tag": SOURCE_TAG,
            "jvm_source_adaptation": False,
        },
    }
    lock[WSL_LOCK_CONTENT_FIELD] = lock_content_sha256(lock)
    return lock


def _canonical_rerender(lock: dict[str, Any]) -> tuple[str, str]:
    """Re-render the lock with only a new timestamp and recompute its hash."""
    rerender = json.loads(json.dumps(lock))
    timestamp = datetime.now(timezone.utc).isoformat()
    if timestamp == lock.get("acquired_at_utc"):
        timestamp = (datetime.now(timezone.utc) + timedelta(microseconds=1)).isoformat()
    rerender["acquired_at_utc"] = timestamp
    return lock_content_sha256(rerender), timestamp


def _write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    temporary.replace(path)


def _default_overlay_source() -> Path:
    return Path(__file__).resolve().parents[2] / "src" / "Jiusi-pys" / "openjdk_ohos"


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    workspace = root.parent
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--distro", help="WSL distribution name (default: the first live one)")
    parser.add_argument("--base", type=Path, default=root / "out" / "f106-wsl-toolchain", help="workflow-owned acquisition root")
    parser.add_argument("--linux-root", help="Linux-side workspace root (default: $HOME/mdds-f106 in the distribution)")
    parser.add_argument("--lock", type=Path, default=root / "scripts" / "java" / "wsl_toolchain.lock.json")
    parser.add_argument("--receipt", type=Path, default=root / DEFAULT_RECEIPT_REL)
    parser.add_argument(
        "--overlay-source",
        default=None,
        help="K working tree supplying the F106 overlay (default: ros2/src/Jiusi-pys/openjdk_ohos)",
    )
    parser.add_argument("--sdk-native-url", help="explicit Linux OHOS SDK native package URL")
    parser.add_argument("--sdk-toolchains-url", help="explicit Linux OHOS SDK toolchains package URL")
    parser.add_argument("--boot-jdk-url", help="explicit Linux x86_64 JDK 21 boot JDK URL")
    args = parser.parse_args()
    if args.overlay_source is None:
        args.overlay_source = str(_default_overlay_source())
    args.started_at_utc = datetime.now(timezone.utc).isoformat()

    lock_path = args.lock.resolve()
    receipt_path = args.receipt.resolve()
    recorder = Recorder()
    receipt: dict[str, Any] = {
        "schema": RECEIPT_SCHEMA,
        "result": "FAIL",
        "distro": args.distro,
        "started_at_utc": args.started_at_utc,
        "completed_at_utc": args.started_at_utc,
        "lock": str(lock_path),
        "lock_written": False,
        "hash_contract": dict(WSL_LOCK_HASH_CONTRACT),
        "reproducibility": None,
        "commands": recorder.commands,
        "probes": recorder.probes,
        "non_finding": "",
    }
    try:
        lock = acquire(args, recorder)
    except AcquisitionError as exc:
        receipt["result"] = exc.classification
        receipt["failure_kind"] = exc.failure_kind
        receipt["error"] = str(exc)
        # AD-7: record which closed-set token produced the verdict, so the receipt
        # shows why the classification was made (`None` when no token matched).
        receipt["matched_token"] = exc.matched_token
        receipt["completed_at_utc"] = datetime.now(timezone.utc).isoformat()
        receipt["non_finding"] = (
            f"the acquisition stopped at {exc.failure_kind}: no Linux OHOS SDK, clang, toolchain file, target "
            f"libc or boot JDK identity was established for distribution "
            f"{args.distro or '<auto-detected>'!s}; no hash, URL or availability claim is made, and no "
            "substitute SDK, host, or Windows artifact was selected."
        )
        _write_json(receipt_path, receipt)
        print(json.dumps({"schema": RECEIPT_SCHEMA, "result": receipt["result"], "error": str(exc)}, sort_keys=True))
        return 2

    _write_json(lock_path, lock)
    reproduced, rerender_timestamp = _canonical_rerender(lock)
    receipt["result"] = "PASS"
    receipt["distro"] = lock["wsl"]["distribution"]
    receipt["content_sha256"] = lock[WSL_LOCK_CONTENT_FIELD]
    receipt["lock_written"] = True
    receipt["reproducibility"] = {
        "method": "canonical_rerender",
        "content_sha256": reproduced,
        "acquired_at_utc": rerender_timestamp,
        "note": (
            "the lock was re-rendered with only acquired_at_utc changed; the content hash is unchanged, "
            "which is the v5 volatility defect fixed (M4)"
        ),
    }
    receipt["completed_at_utc"] = datetime.now(timezone.utc).isoformat()
    receipt["commands"] = recorder.commands
    receipt["probes"] = recorder.probes
    if reproduced != lock[WSL_LOCK_CONTENT_FIELD]:
        receipt["result"] = "FAIL"
        receipt["non_finding"] = "the lock content hash is not reproducible under the recorded hash contract"
        _write_json(receipt_path, receipt)
        print(json.dumps({"schema": RECEIPT_SCHEMA, "result": "FAIL", "error": receipt["non_finding"]}, sort_keys=True))
        return 2
    _write_json(receipt_path, receipt)
    print(
        json.dumps(
            {
                "schema": RECEIPT_SCHEMA,
                "result": "PASS",
                "lock": str(lock_path),
                "content_sha256": lock[WSL_LOCK_CONTENT_FIELD],
                "distribution": lock["wsl"]["distribution"],
                "sdk_api": lock["sdk"]["packages"]["native"]["apiVersion"],
                "sdk_version": lock["sdk"]["packages"]["native"]["version"],
                "clang_version": lock["sdk"]["clang"]["version"],
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
