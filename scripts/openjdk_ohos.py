#!/usr/bin/env python3
"""Verify and run the bounded F106 OpenJDK/OpenHarmony producer.

The runner is deliberately fail closed: every source, approval, build-host and
toolchain input is checked before the producer is invoked.  It does not
download tools, patch OpenJDK, deploy to a board, or write outside the
selected F106 output directory.

Plan Revision v6 moves the build host from Cygwin to a genuine Linux host
(WSL2).  The Windows SDK / F089 Windows-archive input validation is replaced
by lock-based validation of the Linux inputs recorded at acquisition time by
``scripts/java/acquire_wsl_toolchain.py``; no path in any configure argument,
environment value or recorded build-host field may be a Windows-form or
``/mnt/`` path (C11).
"""

from __future__ import annotations

import argparse
import base64
import ctypes
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


SOURCE_URL = "https://github.com/openjdk/jdk21u.git"
SOURCE_TAG = "jdk-21.0.12+8"
SOURCE_COMMIT = "9de4f68c88a0a1510373f291d1a95b1f6b0db8c8"
LICENSE_EXPRESSION = "GPL-2.0-only WITH Classpath-exception-2.0"
APPROVAL_BUNDLE_SHA256 = "D63735E9CDFB485D789358B34081F0193897BE378E35402E8921114703E30A82"
APPROVAL_LOCK_SHA256 = "B897513838336815BBC6F7F1379B400A791099064481091E688BC1163E342360"
PREREQUISITE_SHA256 = "8088C043F84EFA1E5307823F3F4CA5075B2E9ED890865C5763FCF3CF5706E2FE"
F089_JDK_SHA256 = "9BA963EE2371874A74185D18BC7BB2AB9407DF7683300855ED7606E0662321D0"
SDK_API = "23"
SDK_VERSION = "6.1.0.32"
CLANG_REVISION = "feef13a36e78b7a2ff3e9e3f180a958f2782be1e"
CLANG_VERSION = f"OHOS clang 15.0.4 {CLANG_REVISION}"
TARGET_TRIPLE = "aarch64-linux-ohos"
OPENJDK_TARGET = "aarch64-linux-musl"
BUILD_TRIPLET_OVERRIDE = "x86_64-unknown-linux-gnu"
HOST_TIMEOUT_MINUTES = 180

# The F106 build host is WSL2 (Ubuntu), a genuine Linux kernel and userspace.
# The additive Feature-owned lock written by scripts/java/acquire_wsl_toolchain.py
# is the only accepted evidence; the Cygwin/MSYS hybrid is not an accepted host.
WSL_LOCK_SCHEMA = "mdds.wsl-toolchain-lock/v1"
WSL_LOCK_NAME = "wsl_toolchain.lock.json"
WSL_LOCK_CONTENT_FIELD = "content_sha256"
WSL_LOCK_EXCLUDED_KEYS = ["content_sha256", "acquired_at_utc"]
WSL_LOCK_CANONICALIZATION = "utf-8 json; sort_keys=True; separators=(',', ':'); ensure_ascii=True"
WSL_LOCK_HASH_CONTRACT = {
    "algorithm": "sha256",
    "canonicalization": WSL_LOCK_CANONICALIZATION,
    "excluded_keys": WSL_LOCK_EXCLUDED_KEYS,
}
WSL_HOST_TOOLS = ("make", "autoconf", "python3", "cc", "c++", "zip", "unzip", "file")
WSL_MINIMUM_MAKE_VERSION = (4, 0)

# The overlay allowlist is an O-side constant on purpose (M1).  Comparing the K
# tree against the manifest's own copy of this list would make the subset test
# vacuous.
OVERLAY_PATHS = ("scripts/", "f106.source.lock.json", "f106.build-profile.json", "f106.license.lock.json")
PINNED_SURFACE = ("src", "make", "doc", "test", "bin", "LICENSE", "ADDITIONAL_LICENSE_INFO", "ASSEMBLY_EXCEPTION")

FIXPATH_EVIDENCE_TOKEN = "FIXPATH_BASE"
HEX64 = re.compile(r"^[0-9a-fA-F]{64}$")


class F106Error(RuntimeError):
    """An immutable F106 input or safety contract was not satisfied."""

    classification = "BUILD_OR_DEPLOYMENT_DEFECT"
    evidence: dict[str, Any] | None = None


class BuildHostPreconditionError(F106Error):
    """The build-host acquisition gate has not been satisfied (C11/C12).

    This is a plan/input defect, never ``PORT_GAP_DISCOVERED``.
    """

    classification = "BUILD_HOST_PRECONDITION_MISSING"


class BuildHostStructuralError(F106Error):
    """The effective build OS cannot produce a non-Windows target (C10)."""

    classification = "BUILD_HOST_STRUCTURAL_UNSUPPORTED"


class BuildHostDeclarationError(F106Error):
    """The declared build OS differs from the host actually probed (C7)."""

    classification = "BUILD_HOST_DECLARATION_MISMATCH"


class ToolchainIdentityError(F106Error):
    """The acquired Linux toolchain is not the pinned one (U1 stop rule)."""

    classification = "TOOLCHAIN_IDENTITY_MISMATCH"


class InfrastructurePermissionError(F106Error):
    """WDAC, sandbox, or host policy blocked a required verification step."""

    classification = "INFRASTRUCTURE_PERMISSION_FAILURE"


# --------------------------------------------------------------------------- #
# pure build-host guard (C10/C7) -- mirrored verbatim in the K producer so that
# a direct producer invocation is refused as well (Main's M2 adjudication)
# --------------------------------------------------------------------------- #


def normalise_os(value: object) -> str:
    """Collapse an OS name, uname value or target triple to a build-OS class."""
    text = str(value or "").strip().lower()
    if not text:
        return ""
    if text.startswith(("cygwin", "msys", "mingw")) or "windows" in text:
        return "windows"
    if "linux" in text or text in ("posix", "gnu"):
        return "linux"
    if text.startswith("darwin") or "mac os" in text:
        return "darwin"
    return text


def probe_actual_host_os() -> str:
    """Independent in-shell probe of the OS this process is really running on.

    This is never derived from a declaration: an in-shell ``uname -s`` is used
    whenever it is available, and the interpreter's own platform otherwise.
    """
    if os.name != "nt":
        try:
            completed = subprocess.run(
                ["uname", "-s"],
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                text=True,
                timeout=30,
                check=False,
            )
        except (OSError, subprocess.TimeoutExpired):
            completed = None
        if completed is not None and completed.returncode == 0 and completed.stdout.strip():
            probed = normalise_os(completed.stdout.strip())
            if probed:
                return probed
    return normalise_os(platform.system())


def preflight_build_host(actual_host_os: object, declared_build_os: object, target_os: object) -> dict[str, Any]:
    """Pure, importable fail-closed C10/C7 pre-flight.

    ``actual_host_os`` must come from an independent in-shell probe by the
    caller, never from ``declared_build_os``.  The effective build OS is the OS
    the build actually runs on: a declaration cannot change the kernel, so the
    guarded value is the probed host.

    Returns a mapping with ``admitted`` (bool), ``result`` (str or None),
    ``error``, the three OS fields and, when refused, ``classification_evidence``.
    """
    actual = normalise_os(actual_host_os)
    declared = normalise_os(declared_build_os)
    target = normalise_os(target_os)
    effective = actual
    verdict: dict[str, Any] = {
        "admitted": False,
        "result": None,
        "error": None,
        "actual_host_os": actual,
        "declared_build_os": declared,
        "effective_build_os": effective,
        "target_os": target,
        "classification_evidence": None,
    }

    if not actual or not declared or not target:
        verdict["result"] = "BUILD_HOST_PRECONDITION_MISSING"
        verdict["error"] = (
            "the build host could not be established: "
            f"actual={actual!r} declared={declared!r} target={target!r}"
        )
        verdict["classification_evidence"] = {
            "rule_id": "C10-BUILD-HOST-UNOBSERVED",
            "stage": "preflight",
            "actual_host_os": actual,
            "declared_build_os": declared,
            "target_os": target,
        }
        return verdict

    if actual != declared:
        verdict["result"] = "BUILD_HOST_DECLARATION_MISMATCH"
        verdict["error"] = (
            f"the run declares build OS {declared!r} but the host probed in-shell is {actual!r}; "
            "no hybrid build-host declaration is authorised"
        )
        verdict["classification_evidence"] = {
            "rule_id": "C7-DECLARED-VS-ACTUAL-BUILD-OS",
            "stage": "preflight",
            "actual_host_os": actual,
            "declared_build_os": declared,
            "probe": "independent in-shell uname -s / platform probe",
        }
        return verdict

    if effective == "windows" and target != "windows":
        verdict["result"] = "BUILD_HOST_STRUCTURAL_UNSUPPORTED"
        verdict["error"] = (
            f"the effective build OS is {effective!r} while the target OS is {target!r}: the pinned "
            f"build system is structurally incompatible ({FIXPATH_EVIDENCE_TOKEN} is guarded on the "
            "target OS in make/autoconf/basic.m4:78-79 but consumed on the build OS in "
            "make/autoconf/util_paths.m4:67), so no invocation of configure can produce a "
            "non-Windows target from a Windows build OS"
        )
        verdict["classification_evidence"] = {
            "rule_id": "C10-EFFECTIVE-BUILD-OS-WINDOWS-NON-WINDOWS-TARGET",
            "stage": "preflight",
            "structural_limitation": FIXPATH_EVIDENCE_TOKEN,
            "references": ["make/autoconf/basic.m4:78-79", "make/autoconf/util_paths.m4:67"],
            "effective_build_os": effective,
            "target_os": target,
        }
        return verdict

    verdict["admitted"] = True
    return verdict


# --------------------------------------------------------------------------- #
# small helpers
# --------------------------------------------------------------------------- #


def _read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise F106Error(f"cannot read JSON {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise F106Error(f"JSON root must be an object: {path}")
    return value


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
    except OSError as exc:
        raise F106Error(f"cannot hash {path}: {exc}") from exc
    return digest.hexdigest().upper()


def _write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    temporary.replace(path)


def _run(
    command: list[str], cwd: Path, timeout: int = 60, stdin_payload: str | None = None
) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            command,
            cwd=cwd,
            input=stdin_payload,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=timeout,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise F106Error(f"command could not complete: {command[0]}: {exc}") from exc


def _require_hash(path: Path, expected: str, label: str) -> None:
    if not path.is_file():
        raise F106Error(f"missing {label}: {path}")
    actual = _sha256(path)
    if actual != expected:
        raise F106Error(f"{label} SHA-256 mismatch: expected {expected}, got {actual}")


def _git(k_root: Path, *arguments: str) -> str:
    completed = _run(["git", "-C", str(k_root), *arguments], cwd=k_root, timeout=30)
    if completed.returncode != 0:
        raise F106Error(f"git {' '.join(arguments)} failed ({completed.returncode}):\n{completed.stdout}")
    return completed.stdout.strip()


def _version_tuple(text: object) -> tuple[int, ...]:
    match = re.search(r"(\d+(?:\.\d+)*)", str(text))
    if not match:
        raise BuildHostPreconditionError(f"build-host verification record has no version: {text!r}")
    return tuple(int(part) for part in match.group(1).split("."))


def _absolute_posix(label: str, value: object) -> str:
    """Fail closed on any Windows-form, /mnt, or non-absolute recorded path (C11)."""
    text = str(value or "")
    if not text.startswith("/"):
        raise F106Error(f"{label} is not an absolute POSIX path: {text!r}")
    if re.search(r"[A-Za-z]:[\\/]", text) or "\\" in text:
        raise F106Error(f"{label} carries a Windows path form: {text!r}")
    if text.startswith("/mnt/"):
        raise F106Error(f"{label} lives under /mnt (the build must use the Linux filesystem): {text!r}")
    return text


# Script text is never passed to wsl.exe as a command-line word.  WSL's interop
# layer re-runs that command line through a shell *before* the intended inner
# ``bash -lc`` parses it, so every ``$VAR`` in an argv word is substituted
# against the outer environment first, and quoting or backslash escaping does
# not help because the outer parse consumes the backslash too.  The script
# therefore travels base64-encoded on **stdin**, where no host-side parse layer
# can reach it.  ``wsl_script_invocation`` is the single constructor of that
# argv for this runner; the identical transport lives in
# ``scripts/java/acquire_wsl_toolchain.py`` (one helper per module, because the
# contract tests load these modules by path with no sibling import on
# ``sys.path``).
SCRIPT_TRANSPORT = "base64-stdin"
SCRIPT_DECODER_PIPELINE = "base64 -d | bash -l"
WSL_SCRIPT_ARGUMENTS = ["bash", "-lc", SCRIPT_DECODER_PIPELINE]
MANIFEST_HEREDOC = "MDDS_F106_RUN_MANIFEST"


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
        raise F106Error(
            "internal error: the base64 script payload must be a single line, otherwise Windows "
            "newline translation mangles it on the way into the build host"
        )
    return ["wsl.exe", "-d", distro, "--", *WSL_SCRIPT_ARGUMENTS], encoded


def _wsl(distro: str, arguments: list[str], timeout: int = 120, stdin_payload: str | None = None) -> subprocess.CompletedProcess[str]:
    command = ["wsl.exe", "-d", distro, "--", *arguments]
    try:
        return subprocess.run(
            command,
            input=stdin_payload,
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


def _wsl_script(distro: str, script: str, timeout: int = 120) -> subprocess.CompletedProcess[str]:
    """Run a shell script on the build host without putting its text on the command line."""
    _argv, payload = wsl_script_invocation(distro, script)
    return _wsl(distro, list(WSL_SCRIPT_ARGUMENTS), timeout=timeout, stdin_payload=payload)


def _wsl_line(distro: str, script: str, timeout: int = 120) -> str:
    completed = _wsl_script(distro, script, timeout=timeout)
    if completed.returncode != 0:
        raise F106Error(f"wsl {distro} refused {script!r} (exit {completed.returncode}):\n{completed.stdout}")
    lines = [line.strip() for line in completed.stdout.splitlines() if line.strip()]
    return lines[-1] if lines else ""


def _wsl_list_verbose() -> str:
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
    for encoding in ("utf-16-le", "utf-8"):
        try:
            text = raw.decode(encoding).replace("\x00", "")
        except UnicodeDecodeError:
            continue
        if "NAME" in text.upper() or "Ubuntu" in text:
            return text
    return raw.decode("utf-8", errors="replace").replace("\x00", "")


# --------------------------------------------------------------------------- #
# build-host validation (acquisition lock, C11, C12)
# --------------------------------------------------------------------------- #


def _validate_build_host(lock_path: Path) -> dict[str, Any]:
    """Fail closed unless the acquired WSL2/Linux build host is present and live-verified."""
    if not lock_path.is_file():
        raise BuildHostPreconditionError(
            f"F106 requires an acquired WSL2/Linux build host; missing lock: {lock_path} "
            "(run scripts/java/acquire_wsl_toolchain.py — the acquisition gate precedes any configure)"
        )
    try:
        lock = _read_json(lock_path)
    except F106Error as exc:
        raise BuildHostPreconditionError(f"WSL toolchain lock is unreadable: {exc}") from exc
    if lock.get("schema") != WSL_LOCK_SCHEMA:
        raise BuildHostPreconditionError(f"unsupported WSL toolchain lock schema: {lock.get('schema')!r} in {lock_path}")

    recorded_hash = str(lock.get(WSL_LOCK_CONTENT_FIELD, "")).upper()
    if not HEX64.match(recorded_hash):
        raise BuildHostPreconditionError(f"WSL toolchain lock has no well-formed {WSL_LOCK_CONTENT_FIELD}: {lock_path}")
    if lock.get("hash_contract") != WSL_LOCK_HASH_CONTRACT:
        raise BuildHostPreconditionError("WSL toolchain lock does not carry the fixed v6 hash contract")
    if _lock_content_sha256(lock) != recorded_hash:
        raise BuildHostPreconditionError(f"WSL toolchain lock content hash does not match its recorded value: {lock_path}")

    wsl_record = lock.get("wsl")
    if not isinstance(wsl_record, dict):
        raise BuildHostPreconditionError("WSL toolchain lock has no WSL distribution record")
    distribution = str(wsl_record.get("distribution", "")).strip()
    if not distribution:
        raise BuildHostPreconditionError("WSL toolchain lock does not name a distribution")
    if int(wsl_record.get("wsl_version", 0)) != 2:
        raise BuildHostPreconditionError(f"the acquired build host is not WSL version 2: {wsl_record.get('wsl_version')!r}")
    if str(wsl_record.get("uname_s", "")) != "Linux":
        raise BuildHostPreconditionError(f"the acquired build host is not Linux: {wsl_record.get('uname_s')!r}")

    listing = _wsl_list_verbose()
    if distribution not in listing:
        raise BuildHostPreconditionError(
            f"the recorded WSL distribution {distribution!r} is not live on this host:\n{listing}"
        )

    # Independent in-shell probes: the actual build OS and kernel are measured
    # inside the shell that will run configure, never copied from the lock's
    # declared values or from the orchestrating process's own platform.
    live_uname_s = _wsl_line(distribution, "uname -s")
    live_uname_r = _wsl_line(distribution, "uname -r")
    if live_uname_r != str(wsl_record.get("uname_r", "")):
        raise BuildHostPreconditionError(
            f"the live kernel release {live_uname_r!r} does not match the acquired record "
            f"{wsl_record.get('uname_r')!r}; re-run the acquisition"
        )
    declared_build_os = str(lock.get("declared_build_os", "")).strip()

    verdict = preflight_build_host(live_uname_s, declared_build_os, TARGET_TRIPLE)
    if not verdict["admitted"]:
        error_class = {
            "BUILD_HOST_STRUCTURAL_UNSUPPORTED": BuildHostStructuralError,
            "BUILD_HOST_DECLARATION_MISMATCH": BuildHostDeclarationError,
        }.get(str(verdict["result"]), BuildHostPreconditionError)
        error = error_class(str(verdict["error"]))
        error.evidence = verdict.get("classification_evidence")
        raise error

    return {
        "kind": "wsl",
        "distribution": distribution,
        "wsl_version": 2,
        "actual_host_os": "linux",
        "declared_build_os": str(lock.get("declared_build_os", "linux")),
        "effective_build_os": "linux",
        # Recorded for evidence only: the orchestrator necessarily runs on
        # Windows (it drives wsl.exe), while the build itself runs on Linux.
        # This value never feeds the guard above.
        "orchestrator_host_os": probe_actual_host_os(),
        "kernel_release": live_uname_r,
        "bash": "/bin/bash",
        "path_convention": "posix",
        "linux_workspace_root": _absolute_posix("lock linux_workspace_root", lock.get("linux_workspace_root")),
        "linux_tree": _absolute_posix("lock linux_tree", lock.get("linux_tree")),
        "source_commit": SOURCE_COMMIT,
        "build_triplet_override": BUILD_TRIPLET_OVERRIDE,
        "wsl_toolchain_lock_sha256": recorded_hash,
        "overlay_paths": list(OVERLAY_PATHS),
    }


def _lock_content_sha256(lock: dict[str, Any]) -> str:
    """Recompute the WSL lock content hash exactly as the acquisition script does.

    Canonical form: every key except those named by
    ``hash_contract.excluded_keys``, serialized as UTF-8 JSON with sorted keys
    and no insignificant whitespace.  ``acquired_at_utc`` is excluded, so
    re-running the acquisition with only a new timestamp yields the same hash.
    """
    contract = lock.get("hash_contract")
    excluded_keys = WSL_LOCK_EXCLUDED_KEYS
    if isinstance(contract, dict) and isinstance(contract.get("excluded_keys"), list):
        excluded_keys = [str(key) for key in contract["excluded_keys"]]
    excluded = set(excluded_keys)
    payload = {key: value for key, value in lock.items() if key not in excluded}
    encoded = json.dumps(payload, sort_keys=True, separators=(",", ":"), ensure_ascii=True)
    return hashlib.sha256(encoded.encode("utf-8")).hexdigest().upper()


def _validate_lock_sdk(lock: dict[str, Any]) -> dict[str, Any]:
    """Validate the Linux SDK identity recorded by the acquisition (U1 stop rule)."""
    sdk = lock.get("sdk")
    if not isinstance(sdk, dict):
        raise ToolchainIdentityError("the WSL toolchain lock carries no Linux OHOS SDK record")
    root = _absolute_posix("lock sdk.root", sdk.get("root"))
    packages = sdk.get("packages")
    if not isinstance(packages, dict):
        raise ToolchainIdentityError("the Linux OHOS SDK record has no package inventory")
    for name in ("native", "toolchains"):
        package = packages.get(name)
        if not isinstance(package, dict):
            raise ToolchainIdentityError(f"the Linux OHOS SDK record is missing the {name} package")
        if str(package.get("apiVersion")) != SDK_API or str(package.get("version")) != SDK_VERSION:
            raise ToolchainIdentityError(
                f"the acquired Linux OHOS SDK {name} package is not API {SDK_API} / {SDK_VERSION}: "
                f"{package.get('apiVersion')!r} / {package.get('version')!r}"
            )
        if not HEX64.match(str(package.get("sha256", "")).upper()):
            raise ToolchainIdentityError(f"the Linux OHOS SDK {name} package has no well-formed SHA-256")

    clang = sdk.get("clang")
    if not isinstance(clang, dict):
        raise ToolchainIdentityError("the Linux OHOS SDK record has no clang identity")
    version = str(clang.get("version", ""))
    if CLANG_REVISION not in version:
        raise ToolchainIdentityError(
            f"the acquired Linux clang does not self-report the pinned revision: {version!r} "
            f"(expected {CLANG_VERSION!r})"
        )
    clang_path = _absolute_posix("lock sdk.clang.path", clang.get("path"))
    if not clang_path.startswith(root):
        raise ToolchainIdentityError(f"the acquired Linux clang does not resolve inside the SDK: {clang_path}")
    cxx_path = _absolute_posix("lock sdk.clang.cxx_path", clang.get("cxx_path"))

    boot_jdk = lock.get("boot_jdk")
    if not isinstance(boot_jdk, dict):
        raise ToolchainIdentityError("the WSL toolchain lock carries no Linux boot JDK record")
    if str(boot_jdk.get("os", "")).strip().lower() != "linux" or str(boot_jdk.get("arch", "")).strip().lower() not in (
        "x86_64",
        "amd64",
    ):
        raise ToolchainIdentityError(
            f"the acquired boot JDK is not a Linux x86_64 artifact: {boot_jdk.get('os')!r}/{boot_jdk.get('arch')!r}"
        )
    if "21.0.12" not in str(boot_jdk.get("java_version", "")):
        raise ToolchainIdentityError("the acquired Linux boot JDK did not report java 21.0.12")

    target_artifacts = sdk.get("target_artifacts")
    if not isinstance(target_artifacts, dict):
        raise ToolchainIdentityError("the Linux OHOS SDK record has no target-artifact digests")

    return {
        "sdk_root": root,
        "clang_path": clang_path,
        "cxx_path": cxx_path,
        "clang_version": version,
        "clang_sha256": str(clang.get("sha256", "")).upper(),
        "native_package": packages["native"],
        "toolchains_package": packages["toolchains"],
        "boot_jdk": boot_jdk,
        "build_tree": _absolute_posix("lock build_tree.path", (lock.get("build_tree") or {}).get("path")),
    }


# --------------------------------------------------------------------------- #
# K overlay and Linux build-tree validation (M1)
# --------------------------------------------------------------------------- #


def _validate_k_overlay(k_root: Path) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any]]:
    """Validate the overlay repository that supplies the F106 additions."""
    if not (k_root / ".git").exists():
        raise F106Error(f"K is not an independent Git repository: {k_root}")
    present = _run(["git", "-C", str(k_root), "cat-file", "-e", f"{SOURCE_COMMIT}^{{commit}}"], cwd=k_root, timeout=30)
    if present.returncode != 0:
        raise F106Error(f"K does not contain the pinned source commit {SOURCE_COMMIT}:\n{present.stdout}")
    ancestry = _run(["git", "-C", str(k_root), "merge-base", "--is-ancestor", SOURCE_COMMIT, "HEAD"], cwd=k_root, timeout=30)
    if ancestry.returncode != 0:
        raise F106Error(f"K HEAD is not derived from {SOURCE_COMMIT}:\n{ancestry.stdout}")
    remote = _git(k_root, "remote", "get-url", "origin")
    if remote != SOURCE_URL:
        raise F106Error(f"K origin mismatch: expected {SOURCE_URL}, got {remote}")
    status = _run(["git", "-C", str(k_root), "status", "--porcelain", "-uno"], cwd=k_root, timeout=30)
    if status.returncode != 0:
        raise F106Error(f"cannot read the K tracked tree status:\n{status.stdout}")
    if status.stdout.strip():
        raise F106Error(f"the K tracked tree is not clean:\n{status.stdout}")

    source = _read_json(k_root / "f106.source.lock.json")
    expected_source = {
        "schema": "mdds.openjdk-ohos-source-lock/v1",
        "repository": SOURCE_URL,
        "tag": SOURCE_TAG,
        "tag_target_commit": SOURCE_COMMIT,
        "license": LICENSE_EXPRESSION,
        "jvm_source_adaptation": False,
    }
    if source != expected_source:
        raise F106Error("F106 source lock does not match the approved immutable source contract")

    profile = _read_json(k_root / "f106.build-profile.json")
    required_profile = {
        "schema": "mdds.openjdk-ohos-build-profile/v1",
        "target_triple": TARGET_TRIPLE,
        "openjdk_target": OPENJDK_TARGET,
        "libc": "musl",
        "variant": "server",
        "headless": True,
        "release_debug_level": "release",
        "host_timeout_minutes": HOST_TIMEOUT_MINUTES,
        "board_probe_timeout_seconds": 60,
        "image_target": "legacy-jre-image",
    }
    for key, expected in required_profile.items():
        if profile.get(key) != expected:
            raise F106Error(f"F106 build profile mismatch for {key}: {profile.get(key)!r}")
    modules = profile.get("modules")
    if not isinstance(modules, list) or len(modules) != len(set(modules)):
        raise F106Error("F106 module list must be a unique JSON array")
    if not {"java.base", "java.logging"}.issubset(set(modules)):
        raise F106Error("F106 module list omits required headless modules")
    if not 1 <= int(profile.get("jobs", 0)) <= 64:
        raise F106Error("F106 jobs value is outside the bounded range")
    if not 1024 <= int(profile.get("memory_limit_mib", 0)) <= 262144:
        raise F106Error("F106 memory limit is outside the bounded range")

    licenses = _read_json(k_root / "f106.license.lock.json")
    if licenses.get("schema") != "mdds.openjdk-ohos-license-lock/v1":
        raise F106Error("unsupported F106 license lock schema")
    if licenses.get("spdx_expression") != LICENSE_EXPRESSION:
        raise F106Error("F106 license expression does not match the approved source")
    expected_license_files = {"LICENSE", "ADDITIONAL_LICENSE_INFO", "ASSEMBLY_EXCEPTION"}
    records = licenses.get("source_files")
    if not isinstance(records, list) or {str(item.get("path")) for item in records} != expected_license_files:
        raise F106Error("F106 license file inventory is incomplete")
    for item in records:
        relative = Path(str(item["path"]))
        if relative.is_absolute() or ".." in relative.parts:
            raise F106Error(f"unsafe source license path: {relative}")
        _require_hash(k_root / relative, str(item.get("sha256", "")).upper(), "source license")
    if not str(licenses.get("source_offer", "")).strip():
        raise F106Error("F106 source-offer text is empty")

    producer = k_root / "scripts" / "build_ohos_jre.py"
    if not producer.is_file():
        raise F106Error(f"missing F106 producer: {producer}")
    return source, profile, licenses


def _validate_producer_identity(build_host: dict[str, Any], k_root: Path) -> str:
    """Pin the executed producer (overlay commit + producer blob/working-tree hash)."""
    producer_sha256 = _sha256(k_root / "scripts" / "build_ohos_jre.py")
    overlay_commit = _git(k_root, "rev-parse", "HEAD")
    if not re.fullmatch(r"[0-9a-f]{40}", overlay_commit):
        raise F106Error(f"K HEAD is not a commit sha: {overlay_commit!r}")
    build_host["overlay_commit"] = overlay_commit
    build_host["producer_sha256"] = producer_sha256
    build_host["overlay_paths"] = list(OVERLAY_PATHS)
    if overlay_commit != SOURCE_COMMIT:
        diff = _git(k_root, "diff", "--name-status", SOURCE_COMMIT, overlay_commit)
        added_only = _git(k_root, "diff", "--name-status", "--diff-filter=A", SOURCE_COMMIT, overlay_commit)
        changed = [line.split("\t") for line in diff.splitlines() if line.strip()]
        added = [line.split("\t")[-1] for line in added_only.splitlines() if line.strip()]
        for fields in changed:
            if fields[0] != "A":
                raise F106Error(
                    f"the K overlay is not additions-only: {' '.join(fields)} "
                    "(jvm_source_adaptation must stay false)"
                )
        if sorted(added) != sorted(fields[-1] for fields in changed):
            raise F106Error("the K overlay diff is not consistent with its additions-only filter")
        for path in added:
            if not any(path == entry or (entry.endswith("/") and path.startswith(entry)) for entry in OVERLAY_PATHS):
                raise F106Error(f"the K overlay adds a path outside the O-side allowlist: {path}")
    return producer_sha256


def _validate_linux_tree(distro: str, lock: dict[str, Any]) -> dict[str, Any]:
    """Re-point the compiled-tree validation at the Linux-side checkout (M1)."""
    build_tree = lock.get("build_tree")
    if not isinstance(build_tree, dict):
        raise F106Error("the WSL toolchain lock carries no build tree record")
    tree = _absolute_posix("lock build_tree.path", build_tree.get("path"))
    head = _wsl_line(distro, f"git -C {tree} rev-parse HEAD")
    if head != SOURCE_COMMIT:
        raise F106Error(f"the Linux build tree HEAD is {head!r}, expected {SOURCE_COMMIT}")
    origin = _wsl_line(distro, f"git -C {tree} remote get-url origin")
    if origin != SOURCE_URL:
        raise F106Error(f"the Linux build tree origin is {origin!r}, expected {SOURCE_URL}")
    present = _wsl(distro, ["git", "-C", tree, "cat-file", "-e", f"{SOURCE_COMMIT}^{{commit}}"])
    if present.returncode != 0:
        raise F106Error(f"the Linux build tree lacks {SOURCE_COMMIT}:\n{present.stdout}")
    tracked = _wsl_line(distro, f"git -C {tree} status --porcelain -uno")
    if tracked:
        raise F106Error(f"the Linux build tree tracked surface is dirty:\n{tracked}")
    pinned = _wsl_line(distro, f"git -C {tree} status --porcelain -uno -- {' '.join(PINNED_SURFACE)}")
    if pinned:
        raise F106Error(f"the Linux build tree pinned surface is dirty:\n{pinned}")
    configure = _wsl_line(distro, f"test -x {tree}/configure && echo OK")
    if configure != "OK":
        raise F106Error(f"the Linux build tree has no executable configure script: {tree}")
    return {"path": tree, "head": head, "origin": origin, "tracked_tree_clean": True}


# --------------------------------------------------------------------------- #
# immutable inputs
# --------------------------------------------------------------------------- #


def _validate_approval(approval: Path, approval_lock: Path) -> None:
    _require_hash(approval, PREREQUISITE_SHA256, "F106 prerequisite")
    _require_hash(approval_lock, APPROVAL_LOCK_SHA256, "approval bundle lock")
    lock = _read_json(approval_lock)
    if str(lock.get("bundle_sha256", "")).upper() != APPROVAL_BUNDLE_SHA256:
        raise F106Error("approval bundle identity does not match F106")
    entries = lock.get("entries")
    if not isinstance(entries, list):
        raise F106Error("approval bundle lock has no entry inventory")
    entry = next((item for item in entries if item.get("path") == approval.name), None)
    if not isinstance(entry, dict) or str(entry.get("sha256", "")).upper() != PREREQUISITE_SHA256:
        raise F106Error("approval bundle does not bind the F106 prerequisite")


def _validate_f089(f089_lock: Path) -> dict[str, Any]:
    """Validate the F089 pin (provenance and boot-JDK version), not its Windows archive.

    Under v6 the F089 Windows JDK artifact is no longer a build input: the Linux
    platform counterpart is acquired and pinned in the WSL toolchain lock.
    """
    lock = _read_json(f089_lock)
    try:
        jdk = lock["tools"]["jdk"]
        artifact = jdk["artifact"]
    except (KeyError, TypeError) as exc:
        raise F106Error("F089 lock has no JDK artifact") from exc
    if jdk.get("version") != SOURCE_TAG:
        raise F106Error("F089 boot JDK version does not match F106")
    if str(artifact.get("sha256", "")).upper() != F089_JDK_SHA256:
        raise F106Error("F089 boot JDK checksum does not match F106")
    return lock


def _port_inventory(k_root: Path) -> dict[str, Any]:
    platform = (k_root / "make" / "autoconf" / "platform.m4").read_text(encoding="utf-8")
    config_sub = (k_root / "make" / "autoconf" / "build-aux" / "autoconf-config.sub").read_text(encoding="utf-8")
    return {
        "evidence_kind": "static_tree_inspection",
        "linux_musl_mapping": "*linux*-musl" in platform and "linux-musl*" in config_sub,
        "explicit_ohos_mapping": "ohos" in platform.lower() or "ohos" in config_sub.lower(),
        "platform_file": "make/autoconf/platform.m4",
        "config_sub_file": "make/autoconf/build-aux/autoconf-config.sub",
    }


def verify_inputs(args: argparse.Namespace) -> tuple[dict[str, Any], dict[str, Any]]:
    workspace = args.workspace.resolve()
    k_root = args.k_root.resolve()

    _validate_approval(args.approval.resolve(), args.approval_lock.resolve())

    # The build-host acquisition gate is checked first: it is the precondition
    # that makes every later input meaningful, and it must refuse before any
    # configure invocation (C10/C12).
    lock_path = Path(args.wsl_lock).resolve() if args.wsl_lock else workspace / "scripts" / "java" / WSL_LOCK_NAME
    build_host = _validate_build_host(lock_path)
    lock = _read_json(lock_path)
    sdk = _validate_lock_sdk(lock)

    source, profile, licenses = _validate_k_overlay(k_root)
    f089 = _validate_f089(args.f089_lock.resolve())
    producer_sha256 = _validate_producer_identity(build_host, k_root)
    linux_tree = _validate_linux_tree(build_host["distribution"], lock)

    # Cross-check: the producer materialized into the Linux build tree must be
    # byte-identical to the pinned overlay producer.
    distro = build_host["distribution"]
    linux_producer_sha256 = _wsl_line(distro, f"sha256sum {linux_tree['path']}/scripts/build_ohos_jre.py").split()[0].upper()
    if linux_producer_sha256 != producer_sha256:
        raise F106Error(
            "the producer materialized in the Linux build tree does not match the pinned overlay producer: "
            f"{linux_producer_sha256} != {producer_sha256}"
        )

    port_inventory = _port_inventory(k_root)
    result = {
        "schema": "mdds.openjdk-ohos-input-verification/v1",
        "result": "PASS",
        "source_commit": SOURCE_COMMIT,
        "boot_jdk_version": SOURCE_TAG,
        "boot_jdk_archive": str(sdk["boot_jdk"].get("path", "")),
        "boot_jdk_archive_sha256": str(sdk["boot_jdk"].get("sha256", "")).upper(),
        "clang_sha256": sdk["clang_sha256"],
        "toolchain_file_sha256": str(
            lock["sdk"]["target_artifacts"]["toolchain_file"].get("observed_sha256", "")
        ).upper(),
        "target_libc_sha256": str(lock["sdk"]["target_artifacts"]["target_libc"].get("observed_sha256", "")).upper(),
        "build_host": build_host,
        "wsl_toolchain_lock_sha256": build_host["wsl_toolchain_lock_sha256"],
        "port_inventory": port_inventory,
    }
    context = {
        "workspace": workspace,
        "k_root": k_root,
        "source": source,
        "profile": profile,
        "licenses": licenses,
        "f089": f089,
        "lock": lock,
        "lock_path": lock_path,
        "sdk": sdk,
        "build_host": build_host,
        "linux_tree": linux_tree,
        "port_inventory": port_inventory,
    }
    return result, context


def _within(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


def _prepare_output(output: Path, workspace: Path, clean: bool) -> Path:
    output = output.resolve()
    allowed = (workspace / "out").resolve()
    if output == allowed or not _within(output, allowed):
        raise F106Error(f"F106 output must be a child of {allowed}: {output}")
    if clean and output.exists():
        shutil.rmtree(output)
    if output.exists() and any(output.iterdir()):
        raise F106Error(f"F106 output is not empty (use --clean): {output}")
    output.mkdir(parents=True, exist_ok=True)
    return output


class _MemoryStatus(ctypes.Structure):
    _fields_ = [
        ("dwLength", ctypes.c_ulong),
        ("dwMemoryLoad", ctypes.c_ulong),
        ("ullTotalPhys", ctypes.c_ulonglong),
        ("ullAvailPhys", ctypes.c_ulonglong),
        ("ullTotalPageFile", ctypes.c_ulonglong),
        ("ullAvailPageFile", ctypes.c_ulonglong),
        ("ullTotalVirtual", ctypes.c_ulonglong),
        ("ullAvailVirtual", ctypes.c_ulonglong),
        ("ullAvailExtendedVirtual", ctypes.c_ulonglong),
    ]


def _host_resources() -> dict[str, Any]:
    result: dict[str, Any] = {"logical_cpus": os.cpu_count(), "platform": sys.platform}
    if os.name == "nt":
        status = _MemoryStatus()
        status.dwLength = ctypes.sizeof(status)
        if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
            result["total_physical_memory_mib"] = status.ullTotalPhys // (1024 * 1024)
            result["available_physical_memory_mib"] = status.ullAvailPhys // (1024 * 1024)
    return result


def _build_manifest(context: dict[str, Any], output: Path) -> dict[str, Any]:
    """Assemble the run manifest; every build input comes from the Linux lock."""
    lock = context["lock"]
    sdk = context["sdk"]
    build_host = context["build_host"]
    profile = context["profile"]
    boot_jdk = sdk["boot_jdk"]
    target_artifacts = lock["sdk"]["target_artifacts"]
    linux_root = context["build_host"]["linux_workspace_root"]
    linux_output = f"{linux_root}/out/f106-openjdk-ohos"

    # `linux_output` mirrors the Windows output tree relative to the O repo, so
    # the receipts and the produced image copy back byte-for-byte and every
    # recorded relative path is valid on both sides.
    return {
        "schema": "mdds.openjdk-ohos-run-manifest/v1",
        "created_at_utc": datetime.now(timezone.utc).isoformat(),
        "workspace_root": linux_root,
        "k_root": context["linux_tree"]["path"],
        "output_root": linux_output,
        "windows_output_root": str(output),
        "source": context["source"],
        "build_profile": profile,
        "approval_bundle_sha256": APPROVAL_BUNDLE_SHA256,
        "approval_lock_sha256": APPROVAL_LOCK_SHA256,
        "prerequisite_sha256": PREREQUISITE_SHA256,
        "boot_jdk_version": SOURCE_TAG,
        "boot_jdk_archive": str(boot_jdk.get("path", "")),
        "boot_jdk_archive_sha256": str(boot_jdk.get("sha256", "")).upper(),
        "boot_jdk_root": str(boot_jdk.get("root", "")),
        "boot_jdk_version_output": str(boot_jdk.get("java_version", "")),
        "build_jdk_version": SOURCE_TAG,
        "build_jdk_root": str(boot_jdk.get("root", "")),
        "target_triple": TARGET_TRIPLE,
        "openjdk_target": OPENJDK_TARGET,
        "sdk_api": str(lock["sdk"]["packages"]["native"]["apiVersion"]),
        "sdk_version": str(lock["sdk"]["packages"]["native"]["version"]),
        "sdk_root": sdk["sdk_root"],
        "native_package_sha256": str(lock["sdk"]["packages"]["native"]["sha256"]).upper(),
        "toolchains_package_sha256": str(lock["sdk"]["packages"]["toolchains"]["sha256"]).upper(),
        "clang_version": sdk["clang_version"],
        "clang_sha256": sdk["clang_sha256"],
        "toolchain_file": _absolute_posix("lock toolchain_file.path", target_artifacts["toolchain_file"].get("path")),
        "toolchain_file_sha256": str(target_artifacts["toolchain_file"].get("observed_sha256", "")).upper(),
        "target_libc": _absolute_posix("lock target_libc.path", target_artifacts["target_libc"].get("path")),
        "target_libc_sha256": str(target_artifacts["target_libc"].get("observed_sha256", "")).upper(),
        "target_artifact_delta": {
            name: {
                "expected_sha256": str(record.get("expected_sha256", "")).upper(),
                "observed_sha256": str(record.get("observed_sha256", "")).upper(),
                "delta": bool(record.get("delta")),
            }
            for name, record in target_artifacts.items()
            if isinstance(record, dict)
        },
        "sysroot": f"{sdk['sdk_root']}/sysroot",
        "cc": sdk["clang_path"],
        "cxx": sdk["cxx_path"],
        "build_host": build_host,
        "host_timeout_minutes": profile["host_timeout_minutes"],
        "host_resources": _host_resources(),
        "input_verification": {
            "schema": "mdds.openjdk-ohos-input-verification/v1",
            "result": "PASS",
            "wsl_toolchain_lock_sha256": build_host["wsl_toolchain_lock_sha256"],
            "producer_sha256": build_host["producer_sha256"],
        },
    }


def _copy_evidence_back(distro: str, linux_output: str, output: Path) -> None:
    """Copy Linux-side manifests, logs, receipts and the produced image back out.

    ``/mnt`` is used only to carry evidence out of the Linux filesystem; the
    build itself never runs there.  The Linux-side view of the Windows output
    tree is derived arithmetically from the drive letter — no path-conversion
    tool is invoked anywhere in this Feature.
    """
    windows_output = output.resolve()
    drive = windows_output.drive
    if not re.fullmatch(r"[A-Za-z]:", drive):
        raise F106Error(f"cannot map the F106 output tree onto the build host: {windows_output}")
    rest = windows_output.as_posix().split(":", 1)[1].lstrip("/")
    linux_target = f"/mnt/{drive[0].lower()}/{rest}"
    if not linux_target.startswith("/mnt/") or "\\" in linux_target:
        raise F106Error(f"derived an unusable build-host output path: {linux_target!r}")

    completed = _wsl_script(
        distro,
        f"mkdir -p {json.dumps(linux_target)} && cp -a {json.dumps(linux_output)}/. {json.dumps(linux_target)}/",
        timeout=1800,
    )
    if completed.returncode != 0:
        raise F106Error(f"copying F106 evidence out of the build host failed:\n{completed.stdout}")


def attempt(args: argparse.Namespace) -> int:
    verification, context = verify_inputs(args)
    output = _prepare_output(args.output, context["workspace"], args.clean)

    manifest = _build_manifest(context, output)
    run_manifest = output / "manifests" / "run.json"
    _write_json(run_manifest, manifest)

    distro = context["build_host"]["distribution"]
    linux_output = manifest["output_root"]
    linux_manifest = f"{linux_output}/manifests/run.json"

    # Stage the manifest on the Linux side (the build tree and output root are
    # Linux-native; the Windows tree is only where the evidence lands).  The
    # manifest travels inside the transported script as a quoted heredoc, so it
    # is delivered byte-for-byte: the previous inline `cat >` with the manifest
    # on stdin went through Windows text-mode newline translation, which wrote
    # CRLF into a Linux-side JSON file.  No manifest value can collide with the
    # delimiter -- json.dumps escapes newlines inside string values, so the
    # delimiter can never appear at the start of a body line.
    encoded = json.dumps(manifest, indent=2, sort_keys=True) + "\n"
    stage_script = (
        f"mkdir -p {json.dumps(linux_output + '/manifests')} && "
        f"cat > {json.dumps(linux_manifest)} <<'{MANIFEST_HEREDOC}'\n"
        f"{encoded}{MANIFEST_HEREDOC}\n"
    )
    try:
        staged = _wsl_script(distro, stage_script, timeout=120)
    except InfrastructurePermissionError as exc:
        raise InfrastructurePermissionError(f"cannot stage the run manifest on {distro}: {exc}") from exc
    if staged.returncode != 0:
        raise F106Error(f"cannot stage the run manifest on the Linux build host:\n{staged.stdout}")

    producer = f"{context['linux_tree']['path']}/scripts/build_ohos_jre.py"
    command, producer_payload = wsl_script_invocation(
        distro, f"python3 {producer} --manifest {linux_manifest}"
    )
    completed = _run(
        command,
        cwd=context["workspace"],
        timeout=int(context["profile"]["host_timeout_minutes"]) * 60 + 300,
        stdin_payload=producer_payload,
    )

    _copy_evidence_back(distro, linux_output, output)

    receipt = output / "receipts" / "build.json"
    if not receipt.is_file():
        raise F106Error(f"producer returned {completed.returncode} without a build receipt:\n{completed.stdout}")
    value = _read_json(receipt)
    print(json.dumps(value, sort_keys=True))
    return 0 if value.get("result") == "PASS" else completed.returncode or 3


def _common(parser: argparse.ArgumentParser) -> None:
    root = Path(__file__).resolve().parents[1]
    workspace = root.parent
    parser.add_argument("--workspace", type=Path, default=root)
    parser.add_argument("--k-root", type=Path, default=root / "src" / "Jiusi-pys" / "openjdk_ohos")
    parser.add_argument(
        "--approval",
        type=Path,
        default=workspace / "docs" / "agent" / "approval" / "v3-bundle" / "openjdk-ohos-prerequisite.json",
    )
    parser.add_argument(
        "--approval-lock",
        type=Path,
        default=workspace / "docs" / "agent" / "approval" / "v3-bundle" / "bundle.lock.json",
    )
    parser.add_argument("--f089-lock", type=Path, default=root / "scripts" / "java" / "host_tooling.lock.json")
    parser.add_argument(
        "--wsl-lock",
        type=Path,
        default=None,
        help=f"WSL/Linux toolchain lock (default: <workspace>/scripts/java/{WSL_LOCK_NAME})",
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    verify = subparsers.add_parser("verify-inputs", help="verify all immutable F106 inputs")
    _common(verify)
    verify.add_argument("--json", action="store_true")
    run = subparsers.add_parser("attempt", help="run the bounded host configure/build attempt")
    _common(run)
    root = Path(__file__).resolve().parents[1]
    run.add_argument("--output", type=Path, default=root / "out" / "f106-openjdk-ohos")
    run.add_argument("--clean", action="store_true")
    args = parser.parse_args()
    try:
        if args.command == "verify-inputs":
            result, _ = verify_inputs(args)
            if args.json:
                print(json.dumps(result, sort_keys=True))
            else:
                print("F106_INPUTS=PASS")
            return 0
        return attempt(args)
    except F106Error as exc:
        result = {
            "schema": "mdds.openjdk-ohos-input-verification/v1",
            "result": "FAIL",
            "classification": exc.classification,
            "error": str(exc),
        }
        if exc.evidence is not None:
            result["classification_evidence"] = exc.evidence
        if getattr(args, "json", False):
            print(json.dumps(result, sort_keys=True))
        else:
            print(f"F106_ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
