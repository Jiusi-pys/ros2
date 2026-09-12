#!/usr/bin/env python3
"""Acquire, verify, and pin the F106 Cygwin build host.

F106 configures and builds the pinned OpenJDK ``jdk21u`` tree under Cygwin,
the only non-experimental Windows POSIX layer OpenJDK supports.  Cygwin is not
version-pinnable, so this script is the Feature's own acquisition precondition:
it downloads the upstream installer, records its SHA-256 at download time,
installs the approved minimal package set, records the *resolved* versions via
``cygcheck``, live-verifies the tools the producer depends on, and writes the
lock that ``TEST-F106-04`` gates.

The script is deliberately fail closed and never invents an identity:

* no hash, version, or ``uname`` value is written unless it was observed;
* the lock is written only after every verification record is ``PASS``;
* a download, installer, or tool launch blocked by WDAC / sandbox / network
  policy is reported as ``INFRASTRUCTURE_PERMISSION_FAILURE`` and is never
  worked around.

Usage::

    python scripts/java/acquire_cygwin.py            # acquire and write the lock
    python scripts/java/acquire_cygwin.py --dry-run  # print the resolved plan
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


LOCK_SCHEMA = "mdds.cygwin-lock/v1"
INSTALLER_URL = "https://www.cygwin.com/setup-x86_64.exe"
INSTALLER_FILE = "setup-x86_64.exe"
DEFAULT_SITE = "https://mirrors.kernel.org/sourceware/cygwin/"
USER_AGENT = "mdds-f106-cygwin-acquire/1.0"

# The approved minimal package set from the F106 plan card; make must be >= 4.0.
# The card names the awk *tool*; Cygwin ships it in the gawk *package* (there is
# no package named "awk"), and cygcheck -c lists only the package name.
PACKAGES = (
    "autoconf",
    "make",
    "zip",
    "unzip",
    "bash",
    "coreutils",
    "grep",
    "sed",
    "gawk",
    "tar",
    "findutils",
    "diffutils",
    "gcc-core",
    "gcc-g++",
)
VERIFICATION_TOOLS = ("cygpath", "bash", "make", "autoconf")
MINIMUM_MAKE_VERSION = (4, 0)
CYGWIN_UNAME_PREFIX = "CYGWIN_NT-"
CYGWIN_DRIVE_PREFIX = "/cygdrive/"

CONTENT_SHA256_FIELD = "content_sha256"

# WinError 5 = access denied, 1260 = disabled by policy, 4551 = blocked by policy.
POLICY_WINERRORS = frozenset({5, 1260, 4551})
POLICY_MARKERS = (
    "application control policy",
    "blocked by policy",
    "has been blocked",
    "access is denied",
    "access denied",
    "permission denied",
    "operation not permitted",
    "connection refused",
    "connection reset",
    "unable to connect",
    "network is unreachable",
    "name or service not known",
    "no such host",
    "temporary failure in name resolution",
    "certificate verify failed",
    "ssl:",
    "timed out",
    "timeout",
)


class AcquisitionError(RuntimeError):
    """The Cygwin build host could not be acquired or verified."""

    classification = "BUILD_OR_DEPLOYMENT_DEFECT"


class InfrastructurePermissionError(AcquisitionError):
    """WDAC, sandbox, or network policy blocked a required acquisition step."""

    classification = "INFRASTRUCTURE_PERMISSION_FAILURE"


def content_sha256(lock: dict[str, Any]) -> str:
    """SHA-256 over the lock's stable fields, excluding the hash itself.

    Canonical form: every key except ``content_sha256``, serialized as UTF-8
    JSON with sorted keys and no insignificant whitespace.  The runner
    recomputes this with the identical recipe and fails closed on mismatch.
    """
    payload = {key: value for key, value in lock.items() if key != CONTENT_SHA256_FIELD}
    encoded = json.dumps(payload, sort_keys=True, separators=(",", ":"), ensure_ascii=True)
    return hashlib.sha256(encoded.encode("utf-8")).hexdigest().upper()


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def _version_tuple(text: object) -> tuple[int, ...]:
    match = re.search(r"(\d+(?:\.\d+)*)", str(text))
    if not match:
        raise AcquisitionError(f"unparseable version string: {text!r}")
    return tuple(int(part) for part in match.group(1).split("."))


def _is_policy_or_network_failure(detail: str) -> bool:
    lowered = detail.lower()
    return any(marker in lowered for marker in POLICY_MARKERS)


def _blocked_winerror(exc: BaseException) -> int | None:
    winerror = getattr(exc, "winerror", None)
    if isinstance(winerror, int) and winerror in POLICY_WINERRORS:
        return winerror
    return None


def _run(command: list[str], *, timeout: int, cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    """Run a bounded command, classifying policy blocks as infrastructure."""
    try:
        return subprocess.run(
            command,
            cwd=cwd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        raise AcquisitionError(f"command timed out after {timeout}s: {command[0]}") from exc
    except OSError as exc:
        detail = str(exc)
        winerror = _blocked_winerror(exc)
        if winerror is not None or _is_policy_or_network_failure(detail):
            raise InfrastructurePermissionError(
                f"host policy blocked {command[0]} (winerror={winerror}): {detail}"
            ) from exc
        raise AcquisitionError(f"command could not start: {command[0]}: {exc}") from exc


def _download_installer(url: str, destination: Path, timeout: int) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(destination.suffix + ".part")
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response, temporary.open("wb") as stream:
            shutil.copyfileobj(response, stream)
    except (urllib.error.URLError, urllib.error.HTTPError, OSError, ValueError) as exc:
        temporary.unlink(missing_ok=True)
        return_code = getattr(exc, "code", None)
        raise InfrastructurePermissionError(
            f"could not download {url} (http_status={return_code}): {exc}"
        ) from exc
    if not temporary.is_file() or temporary.stat().st_size == 0:
        temporary.unlink(missing_ok=True)
        raise InfrastructurePermissionError(f"downloaded installer is empty: {url}")

    with temporary.open("rb") as stream:
        magic = stream.read(2)
    if magic != b"MZ":
        temporary.unlink(missing_ok=True)
        raise InfrastructurePermissionError(
            f"{url} did not return a Windows executable (magic={magic!r}); "
            "a proxy or policy interception is the likely cause"
        )
    temporary.replace(destination)


def _ensure_installer(url: str, installer: Path, timeout: int, force: bool) -> str:
    """Return the installer's observed SHA-256, downloading it when needed."""
    if force or not installer.is_file():
        _download_installer(url, installer, timeout)
    return _sha256(installer)


def _bash(root: Path) -> Path:
    return root / "bin" / "bash.exe"


def _bash_output(bash: Path, script: str, *, timeout: int = 120) -> str:
    completed = _run([str(bash), "-lc", script], timeout=timeout, cwd=bash.parent)
    if completed.returncode != 0:
        raise AcquisitionError(
            f"Cygwin bash failed for {script!r} (exit {completed.returncode}):\n{completed.stdout}"
        )
    return completed.stdout.strip()


def _lines(text: str) -> list[str]:
    return [line.strip() for line in text.splitlines() if line.strip()]


def _last_line(text: str) -> str:
    """Last non-empty line: correct for single-value probes and `command -v`."""
    lines = _lines(text)
    return lines[-1] if lines else ""


def _first_line(text: str) -> str:
    """First non-empty line: correct for `--version` banners, whose tail is legal text."""
    lines = _lines(text)
    return lines[0] if lines else ""


def _tool_windows_path(bash: Path, cygpath: Path, name: str) -> str:
    posix = _last_line(_bash_output(bash, f"command -v {name}"))
    if not posix.startswith("/"):
        raise AcquisitionError(f"Cygwin does not provide {name!r} on PATH: {posix!r}")
    converted = _run([str(cygpath), "-w", posix], timeout=60)
    windows = _lines(converted.stdout)
    return windows[-1] if converted.returncode == 0 and windows else posix


def _verify_toolchain(root: Path) -> dict[str, Any]:
    """Live-verify every tool the producer depends on and record what was seen."""
    bash = _bash(root)
    if not bash.is_file():
        raise AcquisitionError(f"Cygwin installation is incomplete; missing: {bash}")
    cygpath = root / "bin" / "cygpath.exe"
    if not cygpath.is_file():
        raise AcquisitionError(f"Cygwin installation is incomplete; missing: {cygpath}")

    uname = _last_line(_bash_output(bash, "uname -s"))
    if not uname.startswith(CYGWIN_UNAME_PREFIX):
        raise AcquisitionError(
            f"the acquired bash is not Cygwin: {bash} reports uname -s = {uname!r}"
        )
    bash_version = _last_line(_bash_output(bash, 'printf "%s" "$BASH_VERSION"'))

    probe_input = "C:/Windows"
    probe_output = _last_line(_bash_output(bash, f"cygpath -u '{probe_input}'"))
    if not probe_output.startswith(CYGWIN_DRIVE_PREFIX):
        raise AcquisitionError(
            f"cygpath does not use the required {CYGWIN_DRIVE_PREFIX} form: "
            f"{probe_input!r} -> {probe_output!r}"
        )

    make_banner = _first_line(_bash_output(bash, "make --version"))
    make_version = _version_tuple(make_banner)
    if make_version < MINIMUM_MAKE_VERSION:
        raise AcquisitionError(f"Cygwin make is older than the required 4.0: {make_banner!r}")

    autoconf_banner = _first_line(_bash_output(bash, "autoconf --version"))
    _version_tuple(autoconf_banner)

    return {
        "cygpath": {
            "result": "PASS",
            "path": str(cygpath),
            "command": [str(cygpath), "-w", "<path>"],
            "probe": {"input": probe_input, "output": probe_output},
        },
        "bash": {
            "result": "PASS",
            "path": str(bash),
            "version": bash_version,
            "uname": uname,
            "command": [str(bash), "-lc", "uname -s"],
        },
        "make": {
            "result": "PASS",
            "path": _tool_windows_path(bash, cygpath, "make"),
            "version": ".".join(str(part) for part in make_version),
            "command": [str(bash), "-lc", "make --version"],
            "output": make_banner,
        },
        "autoconf": {
            "result": "PASS",
            "path": _tool_windows_path(bash, cygpath, "autoconf"),
            "version": ".".join(str(part) for part in _version_tuple(autoconf_banner)),
            "command": [str(bash), "-lc", "autoconf --version"],
            "output": autoconf_banner,
        },
        "uname": uname,
    }


def _cygcheck_packages(root: Path) -> dict[str, dict[str, str]]:
    """Record the resolved version of every requested package via cygcheck."""
    bash = _bash(root)
    completed = _run([str(bash), "-lc", "cygcheck -c"], timeout=300, cwd=bash.parent)
    if completed.returncode != 0:
        raise AcquisitionError(
            f"cygcheck -c failed (exit {completed.returncode}):\n{completed.stdout}"
        )
    requested = set(PACKAGES)
    resolved: dict[str, dict[str, str]] = {}
    for line in completed.stdout.splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[0] in requested:
            resolved[fields[0]] = {"version": fields[1], "status": fields[2]}
    missing = sorted(name for name in requested if resolved.get(name, {}).get("status") != "OK")
    if missing:
        raise AcquisitionError(f"Cygwin package set is incomplete or not OK: {missing}")
    return resolved


def _install(root: Path, cache: Path, installer: Path, site: str, timeout: int) -> None:
    package_cache = cache / "packages"
    package_cache.mkdir(parents=True, exist_ok=True)
    command = [
        str(installer),
        "--quiet-mode",
        "--no-admin",
        "--no-shortcuts",
        "--root",
        str(root),
        "--local-package-dir",
        str(package_cache),
        "--site",
        site,
        "--packages",
        ",".join(PACKAGES),
    ]
    completed = _run(command, timeout=timeout)
    if completed.returncode != 0:
        detail = completed.stdout or ""
        if _is_policy_or_network_failure(detail):
            raise InfrastructurePermissionError(
                f"Cygwin installer was blocked (exit {completed.returncode}):\n{detail}"
            )
        raise AcquisitionError(
            f"Cygwin installer failed (exit {completed.returncode}):\n{detail}"
        )
    if not _bash(root).is_file():
        raise AcquisitionError(
            f"Cygwin installer reported success but {_bash(root)} is missing:\n{completed.stdout}"
        )


def _write_lock(path: Path, lock: dict[str, Any]) -> dict[str, Any]:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(lock, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    temporary.replace(path)
    return lock


def acquire(args: argparse.Namespace) -> dict[str, Any]:
    base = args.base.resolve()
    root = args.root.resolve() if args.root else base / "root"
    cache = args.cache.resolve() if args.cache else base / "cache"
    installer = cache / INSTALLER_FILE
    lock_path = args.lock.resolve()

    if args.dry_run:
        return {
            "schema": LOCK_SCHEMA,
            "result": "DRY_RUN",
            "installer_url": INSTALLER_URL,
            "installer_cache": str(installer),
            "root": str(root),
            "package_cache": str(cache / "packages"),
            "site": args.site,
            "packages": list(PACKAGES),
            "verification_tools": list(VERIFICATION_TOOLS),
            "lock": str(lock_path),
        }

    installer_sha256 = _ensure_installer(INSTALLER_URL, installer, args.download_timeout, args.force_download)
    _install(root, cache, installer, args.site, args.install_timeout)

    verification = _verify_toolchain(root)
    uname = str(verification.pop("uname"))
    packages = _cygcheck_packages(root)

    lock: dict[str, Any] = {
        "schema": LOCK_SCHEMA,
        "acquired_at_utc": datetime.now(timezone.utc).isoformat(),
        "installer": {
            "url": INSTALLER_URL,
            "file": INSTALLER_FILE,
            "sha256": installer_sha256,
            "size": installer.stat().st_size,
        },
        "root": str(root),
        "site": args.site,
        "requested_packages": list(PACKAGES),
        "packages": packages,
        "verification": verification,
        "uname": uname,
    }
    lock[CONTENT_SHA256_FIELD] = content_sha256(lock)
    _write_lock(lock_path, lock)
    return {
        "schema": LOCK_SCHEMA,
        "result": "PASS",
        "lock": str(lock_path),
        "content_sha256": lock[CONTENT_SHA256_FIELD],
        "installer_sha256": installer_sha256,
        "uname": uname,
        "make_version": verification["make"]["version"],
    }


def main() -> int:
    default_base = Path(__file__).resolve().parents[2] / "out" / "f106-cygwin"
    default_lock = Path(__file__).resolve().parent / "cygwin.lock.json"
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--base", type=Path, default=default_base, help="workflow-owned acquisition root")
    parser.add_argument("--root", type=Path, help="Cygwin installation root (default: <base>/root)")
    parser.add_argument("--cache", type=Path, help="installer/package cache (default: <base>/cache)")
    parser.add_argument("--lock", type=Path, default=default_lock, help="lock file to write")
    parser.add_argument("--site", default=DEFAULT_SITE, help="Cygwin download mirror")
    parser.add_argument("--download-timeout", type=int, default=600)
    parser.add_argument("--install-timeout", type=int, default=3600)
    parser.add_argument("--force-download", action="store_true", help="re-download the installer")
    parser.add_argument("--dry-run", action="store_true", help="print the resolved plan and exit")
    args = parser.parse_args()
    try:
        print(json.dumps(acquire(args), sort_keys=True))
        return 0
    except AcquisitionError as exc:
        print(
            json.dumps(
                {
                    "schema": LOCK_SCHEMA,
                    "result": "FAIL",
                    "classification": exc.classification,
                    "error": str(exc),
                },
                sort_keys=True,
            )
        )
        return 2


if __name__ == "__main__":
    if os.name != "nt":
        print(
            json.dumps(
                {
                    "schema": LOCK_SCHEMA,
                    "result": "FAIL",
                    "classification": "BUILD_OR_DEPLOYMENT_DEFECT",
                    "error": "the F106 Cygwin build host is acquired on Windows",
                },
                sort_keys=True,
            )
        )
        raise SystemExit(2)
    raise SystemExit(main())
