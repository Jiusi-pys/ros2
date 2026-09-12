#!/usr/bin/env python3
"""Contract tests for the F106 pinned OpenJDK OHOS JRE producer (Plan Revision v10).

These tests deliberately contain no JRE build, deployment, or JVM production
logic.  They define the fail-closed K/O file, guard and receipt interfaces that
the approved F106 v10 implementation and the later lease-owning tester must
satisfy.  Every assertion here is derived from the approved v10 plan card
(``docs/agent/plans/F106.md``), from the twelve binding v6 findings, from the v7,
v8 and v9 review records, and from the four binding adjudications of
``docs/agent/reviews/F106-v10-design-review.md``.

**Adjudications implemented here, having precedence over the card's literal
fixture text (the review ruled those texts un-implementable as written):**

1. The counting unit is **CANDIDATE-level everywhere** -- ``rejection_counts``
   records one entry per examined candidate, keyed by its first-match
   disposition.  ``F106.md:426`` still states the superseded "over failed
   dimensions" unit and is **VOID** (v10 review MEDIUM-1).
2. Fixture **(e)** is a disjoint clang-vs-apiVersion tie and **apiVersion wins
   -> branch 1** (AD-2 puts apiVersion before clang); the card's v10 (e) text,
   which says clang wins, is contradicted by AD-2's own tie-break and by (d)
   (v10 review MEDIUM-2).
3. Fixture **(b)**: forward -> clang wins the tie -> branch 2; reverse (the
   reversed precedence order) -> the clang+archive candidate is labelled
   ``rejected_artifact_pin`` by first match -> ``{rejected_artifact_pin: 2}`` ->
   branch 1 (v10 review LOW-1).
4. The ``bundle_member`` freeze is scoped to the schema's own normalisations, and
   the widening's immutability operand is a **named pre-widening archive** (v10
   review LOW-2).

Required v10 contract surface (test-enforced; the implementation must provide it)
---------------------------------------------------------------------------------
O runner ``ros2/scripts/openjdk_ohos.py``:

* ``preflight_build_host(actual_host_os, declared_build_os, target_os) -> dict``
  -- the pure, importable fail-closed C10/C7 guard.  Returns a mapping with at
  least ``admitted`` (bool), ``result`` (str or None), ``error``, and the three
  OS fields ``actual_host_os`` / ``declared_build_os`` / ``effective_build_os``,
  plus ``classification_evidence`` when refused.  ``actual_host_os`` is always
  derived from an independent in-shell probe by the caller, never from the
  declared value.
* ``admissible_candidate(observed, decision_record) -> dict`` and
  ``deviation_consistency(fields) -> dict`` -- the pure AD-2 lattice seams,
  duplicated byte-identically in the K producer with a shared fixture table.

K producer ``ros2/src/Jiusi-pys/openjdk_ohos/scripts/build_ohos_jre.py``:

* ``preflight_build_host(...)`` -- the identical pure guard, so that a direct
  producer invocation (bypassing the O runner) is refused as well.  It is
  evaluated before any ``configure`` invocation.
* ``classify(stage, exit_code, log_text) -> dict`` -- the pure failure
  classifier returning ``{result, error, classification_evidence}`` with
  ``classification_evidence = {rule_id, stage, matched_line, log_sha256}``.
  ``PORT_GAP_DISCOVERED`` requires an observed log signature; the static port
  inventory is supporting evidence only (CODE-003).

Transport (B1, ``F106/U8``): three carriers -- the two production modules above
and this file -- each define ``SCRIPT_TRANSPORT``, ``SCRIPT_DECODER_PIPELINE``
and a local ``wsl_script_invocation()``.  Script text is never an argv word
(CODE-008); it travels base64-encoded on stdin.  There is **no**
``wsl_transport.py``, no ``sys.path`` mutation and no loader change.

O acquisition ``ros2/scripts/java/acquire_wsl_toolchain.py``:

* ``lock_content_sha256(lock) -> str`` -- pure canonical lock hash honouring
  ``hash_contract.excluded_keys``.
* ``boot_jdk_accepts(record) -> bool`` -- pure Linux x86_64 boot-JDK predicate
  (a Windows artifact fails closed).
* CLI ``--distro NAME --base DIR --lock PATH --receipt PATH``; the machine
  readable receipt is written to ``--receipt`` even when the acquisition fails.

The widened decision record ``ros2/out/f106-u1/delta-matrix.json``
(``F106/U0``) is a **predecessor artifact**: it carries the widening marker with
its own ``canonical_content_sha256``, the identity join
``set(bundles) == set(linux_sdk)``, ``availability[]``, the renamed
``frozen_pins`` keys, and no rewritten pre-existing value.
"""

from __future__ import annotations

import ast
import base64
import hashlib
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path, PurePosixPath
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent
K_ROOT = ROOT / 'src' / 'Jiusi-pys' / 'openjdk_ohos'
APPROVAL = WORKSPACE / 'docs' / 'agent' / 'approval' / 'v3-bundle' / 'openjdk-ohos-prerequisite.json'
APPROVAL_LOCK = WORKSPACE / 'docs' / 'agent' / 'approval' / 'v3-bundle' / 'bundle.lock.json'
DEPENDENCY_DAG = WORKSPACE / 'docs' / 'agent' / 'approval' / 'v3-bundle' / 'dependency-dag.json'
F089_LOCK = ROOT / 'scripts' / 'java' / 'host_tooling.lock.json'
WSL_LOCK = ROOT / 'scripts' / 'java' / 'wsl_toolchain.lock.json'
WSL_ACQUIRE = ROOT / 'scripts' / 'java' / 'acquire_wsl_toolchain.py'
WSL_RECEIPT_REL = 'out/f106-wsl-toolchain/receipts/acquire.json'
WSL_RECEIPT = ROOT / 'out' / 'f106-wsl-toolchain' / 'receipts' / 'acquire.json'
OUTPUT = ROOT / 'out' / 'f106-openjdk-ohos'
RUN_MANIFEST = OUTPUT / 'manifests' / 'run.json'
CONFIGURE_COMMAND_RECORD = OUTPUT / 'manifests' / 'configure-command.json'
CONFIGURE_LOG = OUTPUT / 'logs' / 'configure.log'
BUILD_RECEIPT = OUTPUT / 'receipts' / 'build.json'
ELF_RECEIPT = OUTPUT / 'receipts' / 'elf-audit.json'
LICENSE_RECEIPT = OUTPUT / 'receipts' / 'license-audit.json'

SOURCE_LOCK = K_ROOT / 'f106.source.lock.json'
BUILD_PROFILE = K_ROOT / 'f106.build-profile.json'
LICENSE_LOCK = K_ROOT / 'f106.license.lock.json'
PRODUCER = K_ROOT / 'scripts' / 'build_ohos_jre.py'
O_RUNNER = ROOT / 'scripts' / 'openjdk_ohos.py'

# --- B1: the transport carriers, and the pinned fourth-carrier scan scope ----- #
# Three carriers, one constructor each.  The two production carriers are
# worker-owned for this Feature; this file is the tester-owned third carrier
# (F106/U8) and hosts the cross-carrier assertion because it must load all three.
# The constants and the constructor below are canonical blocks: the cross-carrier
# assertion extracts them BY SYMBOL from all three carriers and requires the
# normalised texts to be identical, so their spelling here must match the
# production carriers' exactly.
SCRIPT_TRANSPORT = "base64-stdin"
SCRIPT_DECODER_PIPELINE = "base64 -d | bash -l"
WSL_SCRIPT_ARGUMENTS = ["bash", "-lc", SCRIPT_DECODER_PIPELINE]

CARRIER_FILES = (
    ROOT / 'scripts' / 'java' / 'acquire_wsl_toolchain.py',
    ROOT / 'scripts' / 'openjdk_ohos.py',
    Path(__file__).resolve(),
)
# Closed symbol corpus (v9 recorded narrowing): these three exist in every
# carrier and are therefore the identity-comparison set.  A literal five-symbol
# equality could never go GREEN, because two of the five exist in one module only.
CANONICAL_TRANSPORT_SYMBOLS = ('SCRIPT_TRANSPORT', 'SCRIPT_DECODER_PIPELINE', 'wsl_script_invocation')
PRESENT_IF_DEFINED_TRANSPORT_SYMBOLS = ('WSL_SCRIPT_ARGUMENTS', 'MANIFEST_HEREDOC')
CANONICAL_WSL_ARGUMENTS = ['bash', '-lc', SCRIPT_DECODER_PIPELINE]
ARGS_EXPRESSION_PLACEHOLDER = '<MODULE_LOCAL_ARGUMENTS>'
# F106 LOW-1: the scan roots, the file kinds and the detection rule are pinned,
# not judged.  A file outside the three named carriers that constructs a wsl.exe
# argv carrying the decoder pipeline is a fourth carrier; a wsl.exe invocation
# *without* the pipeline is not (anti-false-positive rule: the documented
# `git cat-file -e` site and run_ohos_generic_acceptance.sh must not be forced to
# change).  __pycache__ is excluded by construction and must hold no source file.
CARRIER_SCAN_ROOTS = (ROOT / 'scripts', PRODUCER.parent)
CARRIER_SCAN_KINDS = ('.py', '.sh')
CARRIER_SCAN_EXCLUDED_DIRS = ('__pycache__',)
CARRIER_DETECTION_LITERAL = 'base64 -d | bash -l'
CARRIER_ERROR_CLASSES = ('AcquisitionError', 'F106Error', 'AssertionError')
PLAIN_ARGV_FORBIDDEN = ('$', '"', "'", '\\')

# --- v10 decision record, widening contract and AD-2 vocabulary -------------- #
# Every expected value below is read from its cited machine-readable source, by
# path + key; the value-citation rule forbids transcribing a matrix-resident
# value into the test (v8 HIGH, the ERRATUM-1 defect class).
DECISION_RECORD = ROOT / 'out' / 'f106-u1' / 'delta-matrix.json'
DECISION_RECORD_GENERATOR = ROOT / 'out' / 'f106-u1' / 'delta_matrix.py'
# LOW-2 remedy: the immutability operand is a named archive U0 must create
# BEFORE the widening writes, so the freeze is compared, not inspected.
DECISION_RECORD_PRE_WIDENING_ARCHIVE = ROOT / 'out' / 'f106-u1' / 'delta-matrix.pre-widening.json'
DECISION_RECORD_WIDENED_MARKER = 'widened'
DECISION_RECORD_MARKER_DIGEST_FIELD = 'canonical_content_sha256'
DECISION_RECORD_MARKER_EXCLUDED_FIELDS = ('canonical_content_sha256',)
BUNDLE_MEMBER_SUBKEYS = {'native_package', 'toolchains_package'}
# AD-2 (H1): the declared pin set is a closed constant of six names.
DECLARED_PINS = (
    'clang_revision_string',
    'linux_clang_binary',
    'ohos_toolchain_cmake',
    'target_libc',
    'native_sdk_package_archive',
    'toolchains_sdk_package_archive',
)
# AD-2 (H1): total order, first match wins.  Ties are broken by the same order.
AD2_DISPOSITION_ORDER = (
    'unavailable',
    'rejected_api_version',
    'rejected_clang_revision',
    'rejected_artifact_pin',
    'rejected_unrecorded_version',
)
ADOPTED_DISPOSITIONS = ('adopted_frozen_exact', 'adopted_version_deviation')
STOP_RULE_BRANCHES = ('none', '1_no_admissible_sdk', '2_clang_revision_mismatch', '3_version_label_accepted')
COMPARABILITY_REASONS = (
    'different_platform',
    'different_declared_version',
    'no_operand_for_declared_version',
    'abbreviated_operand',
    'kind_mismatch',
)

SOURCE_URL = 'https://github.com/openjdk/jdk21u.git'
SOURCE_TAG = 'jdk-21.0.12+8'
SOURCE_COMMIT = '9de4f68c88a0a1510373f291d1a95b1f6b0db8c8'
LICENSE_EXPRESSION = 'GPL-2.0-only WITH Classpath-exception-2.0'
APPROVAL_BUNDLE_SHA256 = 'D63735E9CDFB485D789358B34081F0193897BE378E35402E8921114703E30A82'
APPROVAL_LOCK_SHA256 = 'B897513838336815BBC6F7F1379B400A791099064481091E688BC1163E342360'
PREREQUISITE_SHA256 = '8088C043F84EFA1E5307823F3F4CA5075B2E9ED890865C5763FCF3CF5706E2FE'
DEPENDENCY_DAG_SHA256 = '86F5E7E103D70DA80D7F94E0594853A1D17B9754AFACCA0AF35568EBE9715538'
DEPENDENCY_DAG_NODES = 106
DEPENDENCY_DAG_EDGES = 521
F106_PREDECESSORS = ('F001', 'F089')
F089_JDK_SHA256 = '9BA963EE2371874A74185D18BC7BB2AB9407DF7683300855ED7606E0662321D0'

SDK_API = '23'
# The FROZEN declared package version (the Windows pin).  It is NOT the version
# the Linux build host adopts: under DEC-002 the adopted declared version is the
# decision record's adopted value, read by path + key through
# `_decision_record_adopted_version`.  Asserting this frozen value against an
# adopted artifact is precisely the deterministic false STOP that DEC-002
# ERRATUM 1 warns of, and the value-citation rule forbids the transcription.
SDK_VERSION_FROZEN = '6.1.0.32'
CLANG_VERSION = 'OHOS clang 15.0.4 feef13a36e78b7a2ff3e9e3f180a958f2782be1e'
CLANG_REVISION = 'feef13a36e78b7a2ff3e9e3f180a958f2782be1e'
TOOLCHAIN_FILE_SHA256 = '0CE9943DF04C192725B41CD70DBB259EDD21FA9FA8D78DEEE0D6254F4D6FEB18'
TARGET_LIBC_SHA256 = '298FB33338D06F07552606FF1EF2ADBE42202E4CED3AB9400F26FE90BC38887C'
TARGET_TRIPLE = 'aarch64-linux-ohos'
OPENJDK_TARGET = 'aarch64-linux-musl'
STAGING_PREFIX = '/data/local/tmp/mdds-jre21'
HOST_TIMEOUT_MINUTES = 180
BOARD_PROBE_TIMEOUT_SECONDS = 60
BUILD_TRIPLET_OVERRIDE = 'x86_64-unknown-linux-gnu'
TARGETS = (
    '3e01ff55454d202020104033bf453b00',
    '3e01ff55454d202020104433991c3b00',
)

WSL_LOCK_SCHEMA = 'mdds.wsl-toolchain-lock/v1'
WSL_RECEIPT_SCHEMA = 'mdds.wsl-toolchain-acquire-receipt/v1'
BUILD_RECEIPT_SCHEMA = 'mdds.openjdk-ohos-build-receipt/v1'
WSL_LOCK_EXCLUDED_KEYS = ['content_sha256', 'acquired_at_utc']
WSL_LOCK_CANONICALIZATION = "utf-8 json; sort_keys=True; separators=(',', ':'); ensure_ascii=True"
WSL_LOCK_HASH_CONTRACT = {
    'algorithm': 'sha256',
    'canonicalization': WSL_LOCK_CANONICALIZATION,
    'excluded_keys': WSL_LOCK_EXCLUDED_KEYS,
}
WSL_HOST_TOOLS = ('make', 'autoconf', 'python3', 'cc', 'c++', 'zip', 'unzip', 'file')
WSL_TARGET_TOOLS = ('ar', 'nm', 'objcopy', 'strip', 'ld_lld')
WSL_REPRODUCIBILITY_METHODS = ('canonical_rerender', 'second_acquisition')

# The overlay allowlist is an O-side constant on purpose: comparing the K tree
# against the manifest's own copy would make the subset test vacuous (M1).
OVERLAY_PATHS = ('scripts/', 'f106.source.lock.json', 'f106.build-profile.json', 'f106.license.lock.json')
PINNED_SURFACE = ('src', 'make', 'doc', 'test', 'bin', 'LICENSE', 'ADDITIONAL_LICENSE_INFO', 'ASSEMBLY_EXCEPTION')

# Receipt vocabulary (CODE-003).  PORT_GAP_DISCOVERED is never derived from the
# static port inventory; it requires an observed log signature.
BUILD_HOST_REFUSAL_RESULTS = (
    'BUILD_HOST_STRUCTURAL_UNSUPPORTED',
    'BUILD_HOST_DECLARATION_MISMATCH',
    'BUILD_HOST_PRECONDITION_MISSING',
)
# Any refusal that stops the run before a configure invocation; the exact
# component-level classification is asserted through the pure guard seam.
REFUSAL_RESULTS = BUILD_HOST_REFUSAL_RESULTS + (
    'INFRASTRUCTURE_PERMISSION_FAILURE',
    'TOOLCHAIN_IDENTITY_MISMATCH',
)
CLASSIFIER_RESULTS = (
    'PASS',
    'TIMEOUT',
    'INFRASTRUCTURE_PERMISSION_FAILURE',
    'BUILD_OR_DEPLOYMENT_DEFECT',
    'PORT_GAP_DISCOVERED',
    'UNCLASSIFIED_FAILURE',
    'BUILD_HOST_STRUCTURAL_UNSUPPORTED',
    'BUILD_HOST_DECLARATION_MISMATCH',
    'TOOLCHAIN_IDENTITY_MISMATCH',
)
ACQUIRE_FAILURE_RESULTS = ('FAIL', 'INFRASTRUCTURE_PERMISSION_FAILURE')
REMOVED_PORT_GAP_ERROR = (
    'Linux-musl compatibility configure failed; the pinned upstream tree has no explicit OHOS platform mapping.'
)
FIXPATH_EVIDENCE_TOKEN = 'FIXPATH_BASE'
TOOL_RESOLUTION_PREFIX = 'F106_TOOL_RESOLUTION='

# Classifier fixtures (M3).  Each fixture is exercised at both stages.
UNMATCHED_FIXTURE = 'some-unknown-tool: unexpected condition 0x5\nnothing else was observed\n'
FIXPATH_FIXTURE = (
    'COMMAND_JSON=["/bin/bash", "configure", "--openjdk-target=aarch64-linux-musl"]\n'
    'checking openjdk-build os-cpu... windows-x86_64\n'
    f'make/autoconf/basic.m4:78: {FIXPATH_EVIDENCE_TOKEN} is not defined for this build os\n'
    'build/.configure-support/generated-configure.sh: line 12651: import: command not found\n'
    'build/.configure-support/generated-configure.sh: line 12652: verify: command not found\n'
    'configure: error: The path of TOPDIR, which resolves as "/tree", could not be imported.\n'
)
# A log that echoes the *static* port inventory conclusion but carries no
# observed port-gap signature: the CODE-003 defect would label it a port gap.
PORT_GAP_LOOKALIKE_FIXTURE = (
    'port_gap_evidence: explicit_ohos_mapping=false linux_musl_mapping=true\n'
    'The pinned upstream tree has no explicit OHOS platform mapping.\n'
    'collect2: error: ld returned 1 exit status\n'
)

HEX64 = re.compile(r'^[0-9a-fA-F]{64}$')
# UNANCHORED on purpose (M6): a drive-letter path may appear anywhere inside a
# serialized argument, environment value or recorded field.
WINDOWS_DRIVE_FORM = re.compile(r'[A-Za-z]:[\\/]')
MNT_PREFIX = '/mnt/'
VOLATILE_KEY = re.compile(r'_utc$')
REQUIRED_SOURCE_LICENSE_FILES = {'LICENSE', 'ADDITIONAL_LICENSE_INFO', 'ASSEMBLY_EXCEPTION'}


# --------------------------------------------------------------------------- #
# generic helpers
# --------------------------------------------------------------------------- #


def _load(path: Path) -> dict[str, Any]:
    if not path.is_file():
        raise AssertionError(f'missing required F106 artifact: {path}')
    value = json.loads(path.read_text(encoding='utf-8'))
    if not isinstance(value, dict):
        raise AssertionError(f'expected JSON object: {path}')
    return value


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def _sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode('utf-8')).hexdigest().upper()


def _git(*arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ['git', '-C', str(K_ROOT), *arguments],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=60,
        check=False,
    )


def _git_bytes(*arguments: str) -> bytes:
    return subprocess.run(
        ['git', '-C', str(K_ROOT), *arguments],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        timeout=60,
        check=False,
    ).stdout


def _assert_hex64(test: unittest.TestCase, value: object) -> str:
    text = str(value)
    test.assertRegex(text, HEX64)
    return text.upper()


def _parse_version(text: object) -> tuple[int, ...]:
    match = re.search(r'(\d+(?:\.\d+)*)', str(text))
    if not match:
        raise AssertionError(f'unparseable version string: {text!r}')
    return tuple(int(part) for part in match.group(1).split('.'))


def _assert_relative_path(test: unittest.TestCase, value: object) -> Path:
    path = Path(str(value))
    test.assertFalse(path.is_absolute())
    test.assertNotIn('..', path.parts)
    return path


def _assert_target_path(test: unittest.TestCase, value: object) -> str:
    path = PurePosixPath(str(value))
    prefix = PurePosixPath(STAGING_PREFIX)
    test.assertTrue(path.is_absolute())
    test.assertTrue(path == prefix or prefix in path.parents, f'path escapes F106 staging prefix: {path}')
    return path.as_posix()


def _assert_absolute_posix(test: unittest.TestCase, label: str, value: object) -> str:
    text = str(value)
    test.assertTrue(text.startswith('/'), f'{label} is not an absolute POSIX path: {text!r}')
    test.assertIsNone(WINDOWS_DRIVE_FORM.search(text), f'{label} carries a Windows drive form: {text!r}')
    test.assertNotIn('\\', text, f'{label} carries a backslash: {text!r}')
    test.assertFalse(text.startswith(MNT_PREFIX), f'{label} lives under /mnt (Linux filesystem required): {text!r}')
    test.assertFalse(text.endswith('.exe'), f'{label} names a Windows binary: {text!r}')
    return text


def _overlay_allowed(path: str) -> bool:
    return any(path == entry or (entry.endswith('/') and path.startswith(entry)) for entry in OVERLAY_PATHS)


def _in_pinned_surface(path: str) -> bool:
    return any(path == entry or path.startswith(f'{entry}/') for entry in PINNED_SURFACE)


def _flatten_strings(value: Any) -> list[str]:
    """Every string leaf of a nested JSON value, in document order."""
    if isinstance(value, str):
        return [value]
    if isinstance(value, dict):
        found: list[str] = []
        for item in value.values():
            found.extend(_flatten_strings(item))
        return found
    if isinstance(value, (list, tuple)):
        found = []
        for item in value:
            found.extend(_flatten_strings(item))
        return found
    return []


def _flatten_keys(value: Any) -> list[str]:
    if isinstance(value, dict):
        found: list[str] = []
        for key, item in value.items():
            found.append(str(key))
            found.extend(_flatten_keys(item))
        return found
    if isinstance(value, (list, tuple)):
        found = []
        for item in value:
            found.extend(_flatten_keys(item))
        return found
    return []


def _manifest_tree_hash(files: list[dict[str, Any]]) -> str:
    digest = hashlib.sha256()
    for entry in sorted(files, key=lambda item: str(item['path'])):
        digest.update(str(entry['path']).encode('utf-8'))
        digest.update(b'\0')
        digest.update(str(entry['size']).encode('ascii'))
        digest.update(b'\0')
        digest.update(str(entry['sha256']).lower().encode('ascii'))
        digest.update(b'\0')
    return digest.hexdigest().upper()


def _last_json(stdout: str) -> dict[str, Any]:
    for line in reversed(stdout.splitlines()):
        if line.strip():
            value = json.loads(line)
            if not isinstance(value, dict):
                raise AssertionError('F106 input verifier did not return a JSON object')
            return value
    raise AssertionError('F106 input verifier emitted no JSON result')


def _load_module(name: str, path: Path) -> Any:
    if not path.is_file():
        raise AssertionError(f'missing required F106 module: {path}')
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise AssertionError(f'cannot import F106 module: {path}')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _require_callable(test: unittest.TestCase, module: Any, name: str, path: Path) -> Any:
    function = getattr(module, name, None)
    test.assertTrue(
        callable(function),
        f'{path.name} must expose the pure, importable seam {name}() (Plan Revision v10 contract)',
    )
    return function


def _canonical_lock_sha256(lock: dict[str, Any], excluded_keys: list[str]) -> str:
    excluded = set(excluded_keys)
    payload = {key: value for key, value in lock.items() if key not in excluded}
    encoded = json.dumps(payload, sort_keys=True, separators=(',', ':'), ensure_ascii=True)
    return hashlib.sha256(encoded.encode('utf-8')).hexdigest().upper()


def _assert_plain_posix(test: unittest.TestCase, label: str, payload: Any) -> None:
    """Unanchored C11 scan over a serialized argv/env/recorded-field structure."""
    for text in _flatten_strings(payload):
        test.assertIsNone(
            WINDOWS_DRIVE_FORM.search(text),
            f'{label}: Windows drive form in {text!r} (C11 forbids path conversion artifacts)',
        )
        test.assertNotIn('\\', text, f'{label}: backslash in {text!r} (C11 requires POSIX form)')
        test.assertNotIn(MNT_PREFIX, text, f'{label}: /mnt path in {text!r} (Linux filesystem required)')


# --------------------------------------------------------------------------- #
# WSL helpers -- a blocked host policy is infrastructure, never production
#
# Transport (F106/U8, B1).  WSL's interop layer re-parses the launched command
# line through a shell for *every* argv word, not only for ``bash -lc``: it
# expands ``$VAR`` and also consumes backslashes and quotes (CODE-008).  Script
# text is therefore never an argv word -- it travels base64-encoded on **stdin**
# and is executed as ``base64 -d | bash -l``.  The contract tests load these
# modules by path with no sibling on ``sys.path`` (``_load_module`` above), so
# there is no shared ``wsl_transport`` module and there is no ``sys.path``
# mutation: each carrier defines the constructor locally.  This file is the
# tester-owned third carrier, and it hosts the cross-carrier "no divergent copy"
# assertion because it must load all three.
# --------------------------------------------------------------------------- #
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
        raise AssertionError(
            "internal error: the base64 script payload must be a single line, otherwise Windows "
            "newline translation mangles it on the way into the build host"
        )
    return ["wsl.exe", "-d", distro, "--", "bash", "-lc", SCRIPT_DECODER_PIPELINE], encoded


def _wsl(
    distro: str,
    arguments: list[str],
    timeout: int = 120,
    stdin_payload: str | None = None,
) -> subprocess.CompletedProcess[str]:
    """Launch ``wsl.exe``.  ``stdin_payload`` carries the base64 script body."""
    command = ['wsl.exe', '-d', distro, '--', *arguments]
    try:
        return subprocess.run(
            command,
            input=stdin_payload,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding='utf-8',
            errors='replace',
            timeout=timeout,
            check=False,
        )
    except FileNotFoundError as exc:
        raise AssertionError(f'INFRASTRUCTURE_PERMISSION_FAILURE: wsl.exe is unavailable: {exc}') from exc
    except PermissionError as exc:
        raise AssertionError(f'INFRASTRUCTURE_PERMISSION_FAILURE: host policy blocked wsl.exe: {exc}') from exc
    except subprocess.TimeoutExpired as exc:
        raise AssertionError(f'INFRASTRUCTURE_PERMISSION_FAILURE: wsl.exe timed out after {timeout}s') from exc
    except OSError as exc:
        raise AssertionError(f'INFRASTRUCTURE_PERMISSION_FAILURE: wsl.exe could not be started: {exc}') from exc


def _wsl_script(distro: str, script: str, timeout: int = 120) -> subprocess.CompletedProcess[str]:
    """Run a shell script on the build host without putting its text on the command line."""
    _argv, payload = wsl_script_invocation(distro, script)
    return _wsl(distro, list(WSL_SCRIPT_ARGUMENTS), timeout=timeout, stdin_payload=payload)


def _wsl_list_verbose(timeout: int = 60) -> str:
    try:
        completed = subprocess.run(
            ['wsl.exe', '-l', '-v'],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout,
            check=False,
        )
    except PermissionError as exc:
        raise AssertionError(f'INFRASTRUCTURE_PERMISSION_FAILURE: host policy blocked wsl.exe: {exc}') from exc
    except OSError as exc:
        raise AssertionError(f'INFRASTRUCTURE_PERMISSION_FAILURE: wsl.exe could not be started: {exc}') from exc
    except subprocess.TimeoutExpired as exc:
        raise AssertionError(f'INFRASTRUCTURE_PERMISSION_FAILURE: wsl.exe -l -v timed out') from exc
    raw = completed.stdout or b''
    for encoding in ('utf-16-le', 'utf-8'):
        try:
            text = raw.decode(encoding).replace('\x00', '')
        except UnicodeDecodeError:
            continue
        if 'NAME' in text.upper() or 'Ubuntu' in text:
            return text
    return raw.decode('utf-8', errors='replace').replace('\x00', '')


def _live_wsl_distribution() -> str:
    """Name of the first live WSL distribution, as reported by ``wsl -l -v``."""
    for line in _wsl_list_verbose().splitlines():
        stripped = line.strip()
        if not stripped or stripped.upper().startswith('NAME'):
            continue
        tokens = [token for token in stripped.split() if token != '*']
        if tokens:
            return tokens[0]
    raise AssertionError('no live WSL distribution is available for the F106 build host (U8)')


def _wsl_line(distro: str, script: str, timeout: int = 60) -> str:
    completed = _wsl_script(distro, script, timeout=timeout)
    if completed.returncode != 0:
        raise AssertionError(
            f'wsl {distro} refused {script!r} (exit {completed.returncode}):\n{completed.stdout}'
        )
    lines = [line.strip() for line in completed.stdout.splitlines() if line.strip()]
    return lines[-1] if lines else ''


# --------------------------------------------------------------------------- #
# test suites
# --------------------------------------------------------------------------- #


class F106ContractTest(unittest.TestCase):
    """Validate the complete F106 producer and board-evidence contract."""

    maxDiff = None

    # -- shared assertions -------------------------------------------------- #

    def _assert_approved_inputs(self) -> tuple[dict[str, Any], dict[str, Any]]:
        approval = _load(APPROVAL)
        lock = _load(APPROVAL_LOCK)
        self.assertEqual(_sha256(APPROVAL), PREREQUISITE_SHA256)
        self.assertEqual(_sha256(APPROVAL_LOCK), APPROVAL_LOCK_SHA256)
        self.assertEqual(lock.get('bundle_sha256'), APPROVAL_BUNDLE_SHA256)
        entry = next((item for item in lock.get('entries', []) if item.get('path') == APPROVAL.name), None)
        self.assertIsNotNone(entry)
        self.assertEqual(str(entry['sha256']).upper(), PREREQUISITE_SHA256)

        dag = _load(DEPENDENCY_DAG)
        self.assertEqual(_sha256(DEPENDENCY_DAG), DEPENDENCY_DAG_SHA256)
        nodes = dag.get('nodes')
        self.assertIsInstance(nodes, dict)
        self.assertEqual(len(nodes), DEPENDENCY_DAG_NODES)
        self.assertEqual(sum(len(value) for value in nodes.values()), DEPENDENCY_DAG_EDGES)
        self.assertEqual(tuple(nodes.get('F106', ())), F106_PREDECESSORS)
        dag_entry = next((item for item in lock.get('entries', []) if item.get('path') == DEPENDENCY_DAG.name), None)
        self.assertIsNotNone(dag_entry, 'the frozen bundle must still bind the 106-node DAG')
        self.assertEqual(str(dag_entry['sha256']).upper(), DEPENDENCY_DAG_SHA256)
        return approval, _load(F089_LOCK)

    def _assert_k_source(self) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any]]:
        self.assertTrue(K_ROOT.is_dir(), f'missing approved K source checkout: {K_ROOT}')
        self.assertTrue((K_ROOT / '.git').exists(), f'K source is not an independent Git repository: {K_ROOT}')
        present = _git('cat-file', '-e', f'{SOURCE_COMMIT}^{{commit}}')
        self.assertEqual(present.returncode, 0, present.stdout)
        ancestry = _git('merge-base', '--is-ancestor', SOURCE_COMMIT, 'HEAD')
        self.assertEqual(ancestry.returncode, 0, f'K HEAD is not derived from {SOURCE_COMMIT}:\n{ancestry.stdout}')

        source = _load(SOURCE_LOCK)
        profile = _load(BUILD_PROFILE)
        licenses = _load(LICENSE_LOCK)
        self.assertTrue(PRODUCER.is_file(), f'missing K producer interface: {PRODUCER}')
        self.assertTrue(O_RUNNER.is_file(), f'missing O integration interface: {O_RUNNER}')

        self.assertEqual(source.get('schema'), 'mdds.openjdk-ohos-source-lock/v1')
        self.assertEqual(source.get('repository'), SOURCE_URL)
        self.assertEqual(source.get('tag'), SOURCE_TAG)
        self.assertEqual(source.get('tag_target_commit'), SOURCE_COMMIT)
        self.assertEqual(source.get('license'), LICENSE_EXPRESSION)
        self.assertIs(source.get('jvm_source_adaptation'), False)

        self.assertEqual(profile.get('schema'), 'mdds.openjdk-ohos-build-profile/v1')
        self.assertEqual(profile.get('target_triple'), TARGET_TRIPLE)
        self.assertEqual(profile.get('openjdk_target'), OPENJDK_TARGET)
        self.assertEqual(profile.get('libc'), 'musl')
        self.assertEqual(profile.get('variant'), 'server')
        self.assertIs(profile.get('headless'), True)
        self.assertEqual(profile.get('release_debug_level'), 'release')
        self.assertEqual(profile.get('host_timeout_minutes'), HOST_TIMEOUT_MINUTES)
        self.assertEqual(profile.get('board_probe_timeout_seconds'), BOARD_PROBE_TIMEOUT_SECONDS)
        modules = profile.get('modules')
        self.assertIsInstance(modules, list)
        self.assertEqual(len(modules), len(set(modules)))
        self.assertTrue({'java.base', 'java.logging'}.issubset(set(modules)))
        self.assertGreater(int(profile.get('jobs', 0)), 0)
        self.assertGreater(int(profile.get('memory_limit_mib', 0)), 0)

        self.assertEqual(licenses.get('schema'), 'mdds.openjdk-ohos-license-lock/v1')
        self.assertEqual(licenses.get('spdx_expression'), LICENSE_EXPRESSION)
        records = licenses.get('source_files')
        self.assertIsInstance(records, list)
        self.assertEqual({str(item.get('path')) for item in records}, REQUIRED_SOURCE_LICENSE_FILES)
        for item in records:
            path = K_ROOT / _assert_relative_path(self, item['path'])
            self.assertTrue(path.is_file(), f'missing upstream license file: {path}')
            self.assertEqual(_sha256(path), _assert_hex64(self, item.get('sha256')))
        self.assertTrue(str(licenses.get('source_offer', '')).strip())
        return source, profile, licenses

    def _assert_overlay_only_source(self, manifest: dict[str, Any]) -> str:
        """M1: the K commit is proven overlay-only against an O-side allowlist."""
        build_host = manifest.get('build_host')
        self.assertIsInstance(build_host, dict)
        overlay_commit = str(build_host.get('overlay_commit', ''))
        self.assertRegex(overlay_commit, re.compile(r'^[0-9a-f]{40}$'))
        self.assertEqual(build_host.get('source_commit'), SOURCE_COMMIT)

        present = _git('cat-file', '-e', f'{overlay_commit}^{{commit}}')
        self.assertEqual(present.returncode, 0, f'overlay_commit is not a K commit: {present.stdout}')

        granted = list(build_host.get('overlay_paths', []))
        self.assertEqual(
            set(granted), set(OVERLAY_PATHS),
            'build_host.overlay_paths must equal the O-side allowlist constant, not widen it',
        )

        # the whole tracked K tree must be clean before the build, and the
        # executed producer must be the pinned file
        status = _git('status', '--porcelain', '-uno')
        self.assertEqual(status.returncode, 0, status.stdout)
        self.assertEqual(status.stdout.strip(), '', f'K tracked tree is not clean:\n{status.stdout}')
        producer_sha256 = _assert_hex64(self, build_host.get('producer_sha256'))
        self.assertEqual(_sha256(PRODUCER), producer_sha256, 'producer_sha256 must pin the executed producer')

        if overlay_commit == SOURCE_COMMIT:
            # Tester PASS precedes the K overlay commit (card S7), so the overlay
            # can still be a working-tree addition set and there is no commit to
            # diff.  The claim to prove here is narrower but still exact: nothing
            # in the pinned upstream surface may be touched; a modification or
            # deletion would already have failed the clean-tree assertion above.
            porcelain = _git('status', '--porcelain')
            self.assertEqual(porcelain.returncode, 0, porcelain.stdout)
            for line in porcelain.stdout.splitlines():
                if not line.strip():
                    continue
                path = line[3:].strip().strip('"')
                self.assertFalse(
                    _in_pinned_surface(path),
                    f'K working tree modifies the pinned upstream surface: {line}',
                )
            return overlay_commit

        # additions-only: a modification, deletion or rename of upstream source
        # would break jvm_source_adaptation: false.
        diff = _git('diff', '--name-status', SOURCE_COMMIT, overlay_commit)
        self.assertEqual(diff.returncode, 0, diff.stdout)
        changed: list[tuple[str, str]] = []
        for line in diff.stdout.splitlines():
            if not line.strip():
                continue
            fields = line.split('\t')
            changed.append((fields[0], fields[-1]))
        for status_code, path in changed:
            self.assertEqual(
                status_code, 'A',
                f'K overlay is not additions-only: {status_code} {path} (jvm_source_adaptation must stay false)',
            )

        added_only = _git('diff', '--name-status', '--diff-filter=A', SOURCE_COMMIT, overlay_commit)
        self.assertEqual(added_only.returncode, 0, added_only.stdout)
        added_paths = [line.split('\t')[-1] for line in added_only.stdout.splitlines() if line.strip()]
        self.assertEqual(sorted(added_paths), sorted(path for _, path in changed))
        for path in added_paths:
            self.assertTrue(_overlay_allowed(path), f'K overlay adds a path outside the O-side allowlist: {path}')

        # producer blob identity at the overlay commit
        blob = _git_bytes('cat-file', 'blob', f'{overlay_commit}:scripts/build_ohos_jre.py')
        self.assertTrue(blob, f'{overlay_commit} has no scripts/build_ohos_jre.py blob')
        self.assertEqual(
            hashlib.sha256(blob).hexdigest().upper(), producer_sha256,
            'producer_sha256 must equal sha256(git show <overlay_commit>:scripts/build_ohos_jre.py)',
        )
        return overlay_commit

    # -- C10/C7 fail-closed pre-flight (M2, adjudicated by Main) ------------- #

    def _assert_preflight_seam(self, label: str, module: Any, path: Path) -> None:
        preflight = _require_callable(self, module, 'preflight_build_host', path)

        # (c) truthful linux/linux is admitted.
        admitted = preflight(actual_host_os='linux', declared_build_os='linux', target_os=TARGET_TRIPLE)
        self.assertIsInstance(admitted, dict)
        self.assertIs(admitted.get('admitted'), True, f'{label}: truthful linux/linux must be admitted')
        self.assertIsNone(admitted.get('result'), f'{label}: an admitted host carries no failure result')
        self.assertEqual(admitted.get('actual_host_os'), 'linux')
        self.assertEqual(admitted.get('declared_build_os'), 'linux')
        self.assertEqual(admitted.get('effective_build_os'), 'linux')

        # (a) effective build OS windows + non-Windows target -> refused before configure.
        structural = preflight(actual_host_os='windows', declared_build_os='windows', target_os=TARGET_TRIPLE)
        self.assertIs(structural.get('admitted'), False, f'{label}: a windows build OS must be refused')
        self.assertEqual(structural.get('result'), 'BUILD_HOST_STRUCTURAL_UNSUPPORTED')
        self.assertEqual(structural.get('effective_build_os'), 'windows')
        self.assertIn(
            FIXPATH_EVIDENCE_TOKEN, json.dumps(structural, sort_keys=True),
            f'{label}: the refusal must name the {FIXPATH_EVIDENCE_TOKEN} structural limitation',
        )
        self.assertIsInstance(structural.get('classification_evidence'), dict)
        self.assertTrue(str(structural['classification_evidence'].get('rule_id', '')).strip())

        # (b) declared linux on an actual Windows host -> declaration mismatch.
        mismatch = preflight(actual_host_os='windows', declared_build_os='linux', target_os=TARGET_TRIPLE)
        self.assertIs(mismatch.get('admitted'), False, f'{label}: a hybrid declaration must be refused')
        self.assertEqual(mismatch.get('result'), 'BUILD_HOST_DECLARATION_MISMATCH')
        self.assertNotEqual(mismatch.get('effective_build_os'), 'linux')
        self.assertEqual(mismatch.get('actual_host_os'), 'windows')
        self.assertNotEqual(mismatch.get('actual_host_os'), mismatch.get('declared_build_os'))
        self.assertIsInstance(mismatch.get('classification_evidence'), dict)

    def _assert_classifier_seam(self, producer_module: Any) -> None:
        """M3/CODE-003: the producer exposes a pure, fixture-driven classifier."""
        classify = _require_callable(self, producer_module, 'classify', PRODUCER)
        for stage in ('configure', 'build'):
            with self.subTest(stage=stage):
                # unmatched signature -> neutral label, never a confidently wrong one
                unmatched = classify(stage, 1, UNMATCHED_FIXTURE)
                self.assertIsInstance(unmatched, dict)
                self.assertEqual(unmatched.get('result'), 'UNCLASSIFIED_FAILURE', f'{stage}: unmatched log')
                self.assertEqual(unmatched.get('result'), unmatched.get('classification'))
                self.assertNotEqual(unmatched.get('error'), REMOVED_PORT_GAP_ERROR)
                self.assertTrue(str(unmatched.get('error', '')).strip())
                self.assertTrue(
                    any(line and line in str(unmatched.get('error')) for line in UNMATCHED_FIXTURE.splitlines()),
                    f'{stage}: the emitted error must be log-derived',
                )

                # the static-inventory lookalike must never become PORT_GAP_DISCOVERED
                lookalike = classify(stage, 1, PORT_GAP_LOOKALIKE_FIXTURE)
                self.assertNotEqual(
                    lookalike.get('result'), 'PORT_GAP_DISCOVERED',
                    f'{stage}: PORT_GAP_DISCOVERED cannot be derived from the static port inventory (CODE-003)',
                )
                self.assertIn(lookalike.get('result'), CLASSIFIER_RESULTS)

                # a FIXPATH_BASE signature is a structural build-host defect
                fixpath = classify(stage, 1, FIXPATH_FIXTURE)
                self.assertEqual(
                    fixpath.get('result'), 'BUILD_HOST_STRUCTURAL_UNSUPPORTED',
                    f'{stage}: the {FIXPATH_EVIDENCE_TOKEN} signature is a structural host defect',
                )
                self.assertIn(FIXPATH_EVIDENCE_TOKEN, json.dumps(fixpath, sort_keys=True))

                for outcome, log_text in (
                    (unmatched, UNMATCHED_FIXTURE),
                    (lookalike, PORT_GAP_LOOKALIKE_FIXTURE),
                    (fixpath, FIXPATH_FIXTURE),
                ):
                    self.assertIn(outcome.get('result'), CLASSIFIER_RESULTS)
                    evidence = outcome.get('classification_evidence')
                    self.assertIsInstance(evidence, dict)
                    self.assertEqual(evidence.get('stage'), stage)
                    self.assertTrue(str(evidence.get('rule_id', '')).strip())
                    self.assertEqual(str(evidence.get('log_sha256')).upper(), _sha256_text(log_text))
                    if outcome.get('result') == 'PORT_GAP_DISCOVERED':
                        matched = str(evidence.get('matched_line', ''))
                        self.assertTrue(matched, 'PORT_GAP_DISCOVERED requires an observed matched line')
                        self.assertIn(matched, log_text)
                # purity / determinism: the classifier is a pure function
                self.assertEqual(classify(stage, 1, FIXPATH_FIXTURE), fixpath)

    def _assert_refusal_precedes_configure(self, label: str, output: Path, returncode: int, stdout: str) -> None:
        """M2: no configure log, no configure-stage receipt, refusal receipt only."""
        self.assertNotEqual(returncode, 0, f'{label}: a refusable build host must not succeed')
        self.assertFalse(
            (output / 'logs' / 'configure.log').exists(),
            f'{label}: a configure log was written although the guard refused (configure must not be invoked)',
        )
        receipt_path = output / 'receipts' / 'build.json'
        receipt = _load(receipt_path) if receipt_path.is_file() else None
        if receipt is not None:
            self.assertEqual(receipt.get('schema'), BUILD_RECEIPT_SCHEMA)
            self.assertIn(receipt.get('result'), REFUSAL_RESULTS, stdout)
            self.assertNotEqual(receipt.get('stage'), 'configure')
            evidence = receipt.get('classification_evidence')
            self.assertIsInstance(evidence, dict, f'{label}: a refusal receipt must carry classification_evidence')
            self.assertTrue(str(evidence.get('rule_id', '')).strip())
        # the refusal must be observable on a machine-readable channel, otherwise
        # "nothing was written" would be satisfied by any unrelated early failure
        self.assertTrue(
            receipt is not None or any(token in stdout for token in REFUSAL_RESULTS),
            f'{label}: the build-host refusal must carry a refused result and classification_evidence '
            f'(neither a refusal receipt nor a refusal classification was observed):\n{stdout}',
        )

    def _synthetic_producer_manifest(self, output: Path, declared_build_os: str) -> Path:
        directory = Path(tempfile.mkdtemp(prefix='f106-v6-manifest-'))
        self.addCleanup(shutil.rmtree, directory, True)
        manifest = {
            'schema': 'mdds.openjdk-ohos-run-manifest/v1',
            'workspace_root': str(ROOT),
            'k_root': str(K_ROOT),
            'output_root': str(output),
            'target_triple': TARGET_TRIPLE,
            'openjdk_target': OPENJDK_TARGET,
            'build_profile': {'host_timeout_minutes': HOST_TIMEOUT_MINUTES},
            'sysroot': '/nonexistent/f106-preflight-probe/sysroot',
            'build_host': {
                'kind': 'wsl',
                'distribution': _live_wsl_distribution(),
                'declared_build_os': declared_build_os,
            },
        }
        path = directory / 'run.json'
        path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n', encoding='utf-8')
        return path

    # -- TEST-F106-01 ------------------------------------------------------- #

    def test_f106_01_host_side_contract_build_host_bound_and_audit(self) -> None:
        """TEST-F106-01: immutable inputs, WSL build host, C10/C11, bound, audit."""
        approval, f089 = self._assert_approved_inputs()
        source, profile, _ = self._assert_k_source()
        self.assertEqual(f089['tools']['jdk']['version'], SOURCE_TAG)
        self.assertEqual(str(f089['tools']['jdk']['artifact']['sha256']).upper(), F089_JDK_SHA256)

        # --- pure guard seams, exercised before any artifact is required ---- #
        runner_module = _load_module('f106_o_runner', O_RUNNER)
        producer_module = _load_module('f106_k_producer', PRODUCER)
        self._assert_preflight_seam('O runner', runner_module, O_RUNNER)
        self._assert_preflight_seam('K producer', producer_module, PRODUCER)

        # --- classifier seam (CODE-003, M3) --------------------------------- #
        self._assert_classifier_seam(producer_module)

        # --- refusal precedes configure, in both components ---------------- #
        probe_root = Path(tempfile.mkdtemp(dir=ROOT / 'out', prefix='f106-v6-preflight-'))
        self.addCleanup(shutil.rmtree, probe_root, True)
        common = [
            '--workspace',
            str(ROOT),
            '--k-root',
            str(K_ROOT),
            '--approval',
            str(APPROVAL),
            '--approval-lock',
            str(APPROVAL_LOCK),
            '--f089-lock',
            str(F089_LOCK),
        ]
        verified = subprocess.run(
            [sys.executable, str(O_RUNNER), 'verify-inputs', *common, '--json'],
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=300,
            check=False,
        )
        self.assertNotEqual(verified.returncode, 0, verified.stdout)
        runner_result = _last_json(verified.stdout)
        self.assertEqual(runner_result.get('result'), 'FAIL')
        self.assertIn(
            runner_result.get('classification'), BUILD_HOST_REFUSAL_RESULTS,
            'the O runner must fail closed on the build host (acquisition gate) before configure:\n'
            + verified.stdout,
        )

        runner_probe = Path(tempfile.mkdtemp(dir=probe_root, prefix='runner-'))
        runner_probe.rmdir()
        attempted = subprocess.run(
            [sys.executable, str(O_RUNNER), 'attempt', *common, '--output', str(runner_probe)],
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=300,
            check=False,
        )
        self.assertNotEqual(attempted.returncode, 0, attempted.stdout)
        self.assertFalse(
            list(runner_probe.rglob('*')),
            'the O runner wrote evidence although the build host was refused: '
            f'{list(runner_probe.rglob("*"))}',
        )

        producer_probe = Path(tempfile.mkdtemp(dir=probe_root, prefix='producer-'))
        manifest = self._synthetic_producer_manifest(producer_probe, declared_build_os='windows')
        produced = subprocess.run(
            [sys.executable, str(PRODUCER), '--manifest', str(manifest)],
            cwd=K_ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=300,
            check=False,
        )
        self._assert_refusal_precedes_configure('K producer', producer_probe, produced.returncode, produced.stdout)

        # --- run manifest and build host ---------------------------------- #
        run = _load(RUN_MANIFEST)
        self.assertEqual(run.get('schema'), 'mdds.openjdk-ohos-run-manifest/v1')
        self.assertEqual(run.get('source'), source)
        self.assertEqual(run.get('build_profile'), profile)
        self.assertEqual(run.get('approval_bundle_sha256'), APPROVAL_BUNDLE_SHA256)
        self.assertEqual(run.get('approval_lock_sha256'), APPROVAL_LOCK_SHA256)
        self.assertEqual(run.get('prerequisite_sha256'), PREREQUISITE_SHA256)
        self.assertEqual(run.get('target_triple'), TARGET_TRIPLE)
        self.assertEqual(run.get('openjdk_target'), OPENJDK_TARGET)
        self.assertEqual(run.get('sdk_api'), SDK_API)
        # v10: the adopted declared version is read from its cited source, never a literal
        self.assertEqual(run.get('sdk_version'), _decision_record_adopted_version(_load(DECISION_RECORD)))
        self.assertEqual(run.get('clang_version'), CLANG_VERSION)
        self.assertEqual(run.get('boot_jdk_version'), SOURCE_TAG)
        self.assertEqual(run.get('host_timeout_minutes'), HOST_TIMEOUT_MINUTES)

        build_host = run.get('build_host')
        self.assertIsInstance(build_host, dict)
        self.assertEqual(build_host.get('kind'), 'wsl')
        self.assertEqual(build_host.get('wsl_version'), 2)
        self.assertEqual(build_host.get('actual_host_os'), 'linux')
        self.assertEqual(build_host.get('declared_build_os'), 'linux')
        self.assertEqual(build_host.get('effective_build_os'), 'linux')
        self.assertEqual(build_host.get('path_convention'), 'posix')
        self.assertEqual(build_host.get('source_commit'), SOURCE_COMMIT)
        self.assertEqual(build_host.get('build_triplet_override'), BUILD_TRIPLET_OVERRIDE)
        self.assertEqual(set(build_host.get('overlay_paths') or []), set(OVERLAY_PATHS))

        lock = _load(WSL_LOCK)
        self.assertEqual(_assert_hex64(self, build_host.get('wsl_toolchain_lock_sha256')), lock['content_sha256'])
        distribution = str(build_host.get('distribution', ''))
        self.assertTrue(distribution, 'build_host.distribution must be recorded')
        self.assertEqual(distribution, lock['wsl']['distribution'])
        self.assertIn(distribution, _wsl_list_verbose())
        self.assertEqual(_wsl_line(distribution, 'uname -s'), 'Linux')
        self.assertEqual(str(build_host.get('actual_host_os')), _wsl_line(distribution, 'uname -s').lower())
        self.assertEqual(str(build_host.get('kernel_release')), _wsl_line(distribution, 'uname -r'))

        workspace_root = _assert_absolute_posix(self, 'build_host.linux_workspace_root', build_host.get('linux_workspace_root'))
        linux_tree = _assert_absolute_posix(self, 'build_host.linux_tree', build_host.get('linux_tree'))
        self.assertEqual(workspace_root, lock['linux_workspace_root'])
        self.assertEqual(linux_tree, lock['linux_tree'])
        self.assertEqual(linux_tree, lock['build_tree']['path'])
        _assert_absolute_posix(self, 'build_host.bash', build_host.get('bash'))

        # LOW (S2): the Windows SDK / F089 Windows-archive input validation is
        # replaced by lock-based Linux validation; the manifest's build-input
        # fields come from the lock, not from the retired Windows artifacts.
        self.assertEqual(run.get('clang_version'), lock['sdk']['clang']['version'])
        self.assertEqual(run.get('sdk_api'), str(lock['sdk']['packages']['native']['apiVersion']))
        self.assertEqual(run.get('sdk_version'), str(lock['sdk']['packages']['native']['version']))
        self.assertEqual(_assert_hex64(self, run.get('clang_sha256')), str(lock['sdk']['clang']['sha256']).upper())
        self.assertEqual(
            _assert_hex64(self, run.get('toolchain_file_sha256')),
            str(lock['sdk']['target_artifacts']['toolchain_file']['observed_sha256']).upper(),
        )
        self.assertEqual(
            _assert_hex64(self, run.get('target_libc_sha256')),
            str(lock['sdk']['target_artifacts']['target_libc']['observed_sha256']).upper(),
        )
        _assert_absolute_posix(self, 'manifest cc', run.get('cc'))
        _assert_absolute_posix(self, 'manifest cxx', run.get('cxx'))

        self._assert_overlay_only_source(run)

        # --- C11 path form (M6) ------------------------------------------- #
        record = _load(CONFIGURE_COMMAND_RECORD)
        self.assertEqual(record.get('schema'), 'mdds.openjdk-ohos-configure-command/v1')
        command = record.get('command')
        environment = record.get('environment')
        self.assertIsInstance(command, list)
        self.assertIsInstance(environment, dict)
        self.assertEqual(command[0], build_host.get('bash'))
        self.assertIn(f'--build={BUILD_TRIPLET_OVERRIDE}', command, 'the explicit build-triplet override is mandatory')
        _assert_plain_posix(self, 'configure argv', command)
        _assert_plain_posix(self, 'configure environment', environment)

        options = {entry.split('=', 1)[0]: entry.split('=', 1)[1] for entry in command if entry.startswith('--with-') and '=' in entry}
        for option in ('--with-sysroot', '--with-boot-jdk', '--with-build-jdk'):
            self.assertIn(option, options, f'{option} is missing from the configure argv')
            _assert_absolute_posix(self, f'configure {option}', options[option])
        for name in ('CC', 'CXX', 'JAVA_HOME'):
            self.assertIn(name, environment, f'{name} is not recorded in the configure environment')
            _assert_absolute_posix(self, f'configure environment {name}', environment[name])
        self.assertTrue(str(environment.get('PATH', '')).strip())
        for entry in str(environment['PATH']).split(':'):
            if entry:
                _assert_absolute_posix(self, 'configure environment PATH entry', entry)

        # --- tool resolution (LOW: build side native, target side pinned) -- #
        log_text = CONFIGURE_LOG.read_text(encoding='utf-8', errors='replace')
        header = [line for line in log_text.splitlines() if line.startswith(TOOL_RESOLUTION_PREFIX)]
        self.assertTrue(header, f'the configure log header must carry a {TOOL_RESOLUTION_PREFIX} record')
        tools = json.loads(header[0][len(TOOL_RESOLUTION_PREFIX):])
        build_cc = tools.get('build_cc')
        self.assertIsInstance(build_cc, dict)
        _assert_absolute_posix(self, 'tool resolution build_cc.path', build_cc.get('path'))
        self.assertIs(build_cc.get('is_native_x86_64_linux'), True, 'the resolved build compiler must be native x86_64 Linux')
        target_tools = tools.get('target_tools')
        self.assertIsInstance(target_tools, dict)
        sdk_root = _assert_absolute_posix(self, 'lock sdk.root', lock['sdk']['root'])
        for tool in WSL_TARGET_TOOLS:
            self.assertIn(tool, target_tools, f'the tool-resolution record must carry the target tool {tool}')
            resolved = _assert_absolute_posix(self, f'target tool {tool}', target_tools[tool])
            self.assertTrue(resolved.startswith(sdk_root), f'target tool {tool} does not resolve inside the OHOS SDK: {resolved}')

        # --- log-level regression assertions (real run) -------------------- #
        self.assertIn('checking openjdk-build os-cpu... linux-x86_64', log_text)
        self.assertNotIn('x86_64-pc-wsl', log_text)
        self.assertNotIn('import: command not found', log_text)
        self.assertNotIn('verify: command not found', log_text)

        # --- build receipt, ELF and license audit -------------------------- #
        build = _load(BUILD_RECEIPT)
        self.assertEqual(build.get('schema'), BUILD_RECEIPT_SCHEMA)
        self.assertEqual(build.get('result'), 'PASS')
        self.assertEqual(build.get('source_commit'), SOURCE_COMMIT)
        self.assertLessEqual(float(build.get('elapsed_seconds', HOST_TIMEOUT_MINUTES * 60 + 1)), HOST_TIMEOUT_MINUTES * 60)
        self.assertEqual(build.get('timeout_minutes'), HOST_TIMEOUT_MINUTES)
        inventory = build.get('port_gap_evidence')
        self.assertIsInstance(inventory, dict)
        self.assertEqual(inventory.get('evidence_kind'), 'static_tree_inspection')
        self.assertNotEqual(build.get('error'), REMOVED_PORT_GAP_ERROR)

        image_root = ROOT / _assert_relative_path(self, build.get('image_root'))
        self.assertTrue(image_root.is_dir(), f'missing produced JRE image: {image_root}')
        files = build.get('files')
        self.assertIsInstance(files, list)
        self.assertTrue(files)
        seen: set[str] = set()
        for item in files:
            relative = _assert_relative_path(self, item.get('path'))
            self.assertNotIn(relative.as_posix(), seen)
            seen.add(relative.as_posix())
            path = image_root / relative
            self.assertTrue(path.is_file(), f'manifested JRE file is missing: {path}')
            self.assertEqual(path.stat().st_size, item.get('size'))
            self.assertEqual(_sha256(path), _assert_hex64(self, item.get('sha256')))
        tree_hash = _manifest_tree_hash(files)
        self.assertEqual(tree_hash, _assert_hex64(self, build.get('artifact_tree_sha256')))

        elf = _load(ELF_RECEIPT)
        self.assertEqual(elf.get('schema'), 'mdds.openjdk-ohos-elf-audit/v1')
        self.assertEqual(elf.get('result'), 'PASS')
        self.assertEqual(str(elf.get('artifact_tree_sha256')).upper(), tree_hash)
        actual_elf = {
            path.relative_to(image_root).as_posix()
            for path in image_root.rglob('*')
            if path.is_file() and path.read_bytes()[:4] == b'\x7fELF'
        }
        records = elf.get('native_files')
        self.assertIsInstance(records, list)
        self.assertEqual({str(item.get('path')) for item in records}, actual_elf)
        self.assertTrue(records, 'produced JRE must contain audited native ELF files')
        for item in records:
            self.assertEqual(item.get('machine'), 'AArch64')
            self.assertEqual(item.get('abi'), TARGET_TRIPLE)
            self.assertIn(item.get('interpreter'), (None, '/lib/ld-musl-aarch64.so.1'))
            self.assertIsInstance(item.get('needed'), list)
            self.assertEqual(item.get('unresolved_needed'), [])

        license_audit = _load(LICENSE_RECEIPT)
        self.assertEqual(license_audit.get('schema'), 'mdds.openjdk-ohos-license-audit/v1')
        self.assertEqual(license_audit.get('result'), 'PASS')
        self.assertEqual(license_audit.get('spdx_expression'), LICENSE_EXPRESSION)
        self.assertEqual(set(license_audit.get('source_files', [])), REQUIRED_SOURCE_LICENSE_FILES)
        artifact_licenses = license_audit.get('artifact_license_files')
        self.assertIsInstance(artifact_licenses, list)
        self.assertTrue(artifact_licenses, 'produced image must carry a license bundle')
        self.assertTrue(str(license_audit.get('source_offer', '')).strip())

    # -- TEST-F106-02 / TEST-F106-03 --------------------------------------- #

    def test_f106_02_both_board_java_hello_world_and_jni_receipts(self) -> None:
        """TEST-F106-02: the identical image runs Java and JNI twice."""
        build = _load(BUILD_RECEIPT)
        artifact_hash = _assert_hex64(self, build.get('artifact_tree_sha256'))
        for target in TARGETS:
            with self.subTest(target=target):
                receipt = _load(OUTPUT / 'receipts' / 'boards' / target / 'smoke.json')
                self.assertEqual(receipt.get('schema'), 'mdds.openjdk-ohos-board-smoke/v1')
                self.assertEqual(receipt.get('result'), 'PASS')
                self.assertEqual(receipt.get('target'), target)
                self.assertEqual(str(receipt.get('artifact_tree_sha256')).upper(), artifact_hash)
                self.assertEqual(receipt.get('probe_timeout_seconds'), BOARD_PROBE_TIMEOUT_SECONDS)
                self.assertEqual(_assert_target_path(self, receipt.get('staging_prefix')), STAGING_PREFIX)
                probes = receipt.get('probes')
                self.assertIsInstance(probes, dict)
                for name in ('java_version', 'hello_world', 'hello_jni'):
                    probe = probes.get(name)
                    self.assertIsInstance(probe, dict)
                    self.assertEqual(probe.get('exit_code'), 0)
                    self.assertLessEqual(float(probe.get('elapsed_seconds', BOARD_PROBE_TIMEOUT_SECONDS + 1)), BOARD_PROBE_TIMEOUT_SECONDS)
                    self.assertTrue(str(probe.get('command', '')).strip())
                self.assertIn('21.0.12', str(probes['java_version'].get('output', '')))
                self.assertIn('F106_HELLO_WORLD_OK', str(probes['hello_world'].get('output', '')))
                self.assertIn('F106_HELLO_JNI_OK', str(probes['hello_jni'].get('output', '')))
                self.assertEqual(probes['hello_jni'].get('library_abi'), TARGET_TRIPLE)

    def test_f106_03_jni_stress_shutdown_and_staging_containment_receipts(self) -> None:
        """TEST-F106-03: JNI stress and fail-closed staging boundaries."""
        build = _load(BUILD_RECEIPT)
        artifact_hash = _assert_hex64(self, build.get('artifact_tree_sha256'))
        for target in TARGETS:
            with self.subTest(target=target):
                receipt = _load(OUTPUT / 'receipts' / 'boards' / target / 'stress.json')
                self.assertEqual(receipt.get('schema'), 'mdds.openjdk-ohos-board-stress/v1')
                self.assertEqual(receipt.get('result'), 'PASS')
                self.assertEqual(receipt.get('target'), target)
                self.assertEqual(str(receipt.get('artifact_tree_sha256')).upper(), artifact_hash)
                self.assertEqual(receipt.get('probe_timeout_seconds'), BOARD_PROBE_TIMEOUT_SECONDS)
                self.assertLessEqual(float(receipt.get('elapsed_seconds', BOARD_PROBE_TIMEOUT_SECONDS + 1)), BOARD_PROBE_TIMEOUT_SECONDS)

                thread = receipt.get('native_thread')
                self.assertIsInstance(thread, dict)
                self.assertGreaterEqual(int(thread.get('attach_count', 0)), 1)
                self.assertEqual(thread.get('attach_count'), thread.get('callback_count'))
                self.assertEqual(thread.get('attach_count'), thread.get('detach_count'))
                self.assertEqual(thread.get('pending_exception_count'), 0)

                exceptions = receipt.get('exceptions')
                self.assertIsInstance(exceptions, dict)
                self.assertGreaterEqual(int(exceptions.get('propagated_count', 0)), 1)
                self.assertEqual(exceptions.get('unexpected_count'), 0)

                references = receipt.get('references')
                self.assertIsInstance(references, dict)
                self.assertGreaterEqual(int(references.get('local_peak', 0)), 1000)
                self.assertGreaterEqual(int(references.get('global_peak', 0)), 100)
                self.assertEqual(references.get('local_after'), 0)
                self.assertEqual(references.get('global_after'), 0)
                self.assertGreaterEqual(int(references.get('gc_cycles', 0)), 2)

                shutdown = receipt.get('shutdown')
                self.assertIsInstance(shutdown, dict)
                self.assertGreaterEqual(int(shutdown.get('iterations', 0)), 10)
                self.assertEqual(shutdown.get('successful_iterations'), shutdown.get('iterations'))
                self.assertEqual(shutdown.get('abnormal_terminations'), 0)

                containment = receipt.get('staging_containment')
                self.assertIsInstance(containment, dict)
                self.assertEqual(_assert_target_path(self, containment.get('prefix')), STAGING_PREFIX)
                for path in containment.get('written_paths', []):
                    _assert_target_path(self, path)
                self.assertEqual(containment.get('outside_prefix_changes'), [])
                self.assertEqual(containment.get('system_partition_writes'), [])
                self.assertEqual(containment.get('cleanup_residuals'), [])

    # -- TEST-F106-04 ------------------------------------------------------ #

    def test_f106_04_wsl_linux_toolchain_acquisition_and_pin(self) -> None:
        """TEST-F106-04: the WSL/Linux build host inputs are acquired, verified, and pinned."""
        self.assertTrue(WSL_ACQUIRE.is_file(), f'missing F106 WSL acquisition script: {WSL_ACQUIRE}')
        acquisition = _load_module('f106_wsl_acquire', WSL_ACQUIRE)
        lock_content_sha256 = _require_callable(self, acquisition, 'lock_content_sha256', WSL_ACQUIRE)
        boot_jdk_accepts = _require_callable(self, acquisition, 'boot_jdk_accepts', WSL_ACQUIRE)

        lock = _load(WSL_LOCK)
        self.assertEqual(lock.get('schema'), WSL_LOCK_SCHEMA)
        content_sha256 = _assert_hex64(self, lock.get('content_sha256'))

        # --- M4: fixed hash contract, no other volatile field --------------- #
        contract = lock.get('hash_contract')
        self.assertIsInstance(contract, dict)
        self.assertEqual(contract.get('algorithm'), 'sha256')
        self.assertEqual(contract.get('canonicalization'), WSL_LOCK_CANONICALIZATION)
        self.assertEqual(list(contract.get('excluded_keys', [])), WSL_LOCK_EXCLUDED_KEYS)
        self.assertEqual(contract, WSL_LOCK_HASH_CONTRACT)
        for key in _flatten_keys(lock):
            if key in WSL_LOCK_EXCLUDED_KEYS:
                continue
            self.assertIsNone(
                VOLATILE_KEY.search(key),
                f'the lock carries a volatile key {key!r} that the hash contract does not exclude',
            )
        self.assertEqual(_assert_hex64(self, lock_content_sha256(lock)), content_sha256)
        self.assertEqual(_canonical_lock_sha256(lock, WSL_LOCK_EXCLUDED_KEYS), content_sha256)

        # reproducibility is proven from a *recorded* re-render, not an in-memory edit
        receipt = _load(WSL_RECEIPT)
        self.assertEqual(receipt.get('schema'), WSL_RECEIPT_SCHEMA)
        self.assertEqual(receipt.get('result'), 'PASS')
        self.assertEqual(Path(str(lock.get('acquisition_receipt'))).as_posix(), WSL_RECEIPT_REL)
        self.assertEqual(_assert_hex64(self, receipt.get('content_sha256')), content_sha256)
        self.assertEqual(receipt.get('hash_contract'), WSL_LOCK_HASH_CONTRACT)
        reproduction = receipt.get('reproducibility')
        self.assertIsInstance(reproduction, dict)
        self.assertIn(reproduction.get('method'), WSL_REPRODUCIBILITY_METHODS)
        self.assertEqual(_assert_hex64(self, reproduction.get('content_sha256')), content_sha256)
        rerender_timestamp = str(reproduction.get('acquired_at_utc', ''))
        self.assertTrue(rerender_timestamp.strip(), 'the re-render timestamp must be recorded')
        self.assertNotEqual(rerender_timestamp, lock.get('acquired_at_utc'))
        variant = dict(lock)
        variant['acquired_at_utc'] = rerender_timestamp
        self.assertEqual(
            lock_content_sha256(variant), content_sha256,
            'changing only acquired_at_utc must not change the lock content hash (v5 volatility defect)',
        )
        mutated = json.loads(json.dumps(lock))
        mutated['wsl']['distribution'] = str(mutated['wsl']['distribution']) + '-mutated'
        self.assertNotEqual(
            lock_content_sha256(mutated), content_sha256,
            'changing a measured field must change the lock content hash',
        )

        # --- every recorded digest ------------------------------------------ #
        for label, value in _digest_fields(lock):
            self.assertRegex(str(value), HEX64, f'{label} is not a well-formed 64-hex digest')

        # --- WSL identity, recorded and live-re-probed ---------------------- #
        wsl = lock.get('wsl')
        self.assertIsInstance(wsl, dict)
        distribution = str(wsl.get('distribution', ''))
        self.assertTrue(distribution)
        self.assertEqual(wsl.get('wsl_version'), 2)
        self.assertEqual(wsl.get('uname_s'), 'Linux')
        self.assertEqual(lock.get('actual_host_os'), 'linux')
        self.assertEqual(lock.get('declared_build_os'), 'linux')
        self.assertEqual(lock.get('effective_build_os'), 'linux')
        self.assertEqual(lock.get('build_triplet_override'), BUILD_TRIPLET_OVERRIDE)
        self.assertEqual(lock.get('path_convention'), 'posix')
        listing = _wsl_list_verbose()
        self.assertIn(distribution, listing, f'the recorded WSL distribution is not live: {listing!r}')
        self.assertEqual(_wsl_line(distribution, 'uname -s'), 'Linux')
        self.assertEqual(str(wsl.get('uname_r')), _wsl_line(distribution, 'uname -r'))

        # --- Linux SDK identity --------------------------------------------- #
        sdk = lock.get('sdk')
        self.assertIsInstance(sdk, dict)
        sdk_root = _assert_absolute_posix(self, 'sdk.root', sdk.get('root'))
        packages = sdk.get('packages')
        self.assertIsInstance(packages, dict)
        for name in ('native', 'toolchains'):
            package = packages.get(name)
            self.assertIsInstance(package, dict, f'missing Linux SDK {name} package record')
            self.assertEqual(str(package.get('apiVersion')), SDK_API)
            # v10: the acquired packages carry the ADOPTED declared version, read from its cited source
            self.assertEqual(str(package.get('version')), _decision_record_adopted_version(_load(DECISION_RECORD)))
            self.assertTrue(str(package.get('file', '')).strip())
            self.assertGreater(int(package.get('size', 0)), 0)
        clang = sdk.get('clang')
        self.assertIsInstance(clang, dict)
        self.assertEqual(clang.get('version'), CLANG_VERSION)
        self.assertIn(CLANG_REVISION, str(clang.get('version')))
        clang_path = _assert_absolute_posix(self, 'sdk.clang.path', clang.get('path'))
        self.assertTrue(clang_path.startswith(sdk_root))

        # --- target-artifact delta recording (LOW) -------------------------- #
        target_artifacts = sdk.get('target_artifacts')
        self.assertIsInstance(target_artifacts, dict)
        frozen_pins = _load(DECISION_RECORD)['frozen_pins']
        frozen = {
            # cited by key from the decision record's frozen-pin block, never transcribed
            'toolchain_file': str(frozen_pins['ohos_toolchain_cmake_sha256']),
            'target_libc': str(frozen_pins['target_libc_sha256']),
        }
        for name, expected in frozen.items():
            record = target_artifacts.get(name)
            self.assertIsInstance(record, dict, f'missing target artifact record: {name}')
            observed = _assert_hex64(self, record.get('observed_sha256'))
            self.assertEqual(str(record.get('expected_sha256')).upper(), expected)
            self.assertEqual(bool(record.get('delta')), observed != expected)
            _assert_absolute_posix(self, f'target artifact {name}.path', record.get('path'))

        # --- Linux boot JDK -------------------------------------------------- #
        boot_jdk = lock.get('boot_jdk')
        self.assertIsInstance(boot_jdk, dict)
        self.assertEqual(boot_jdk.get('os'), 'linux')
        self.assertEqual(boot_jdk.get('arch'), 'x86_64')
        self.assertEqual(str(boot_jdk.get('version')), SOURCE_TAG)
        self.assertTrue(str(boot_jdk.get('java_version', '')).strip())
        self.assertIn('21.0.12', str(boot_jdk.get('java_version')))
        self.assertTrue(str(boot_jdk.get('url', '')).startswith('https://'))
        self.assertTrue(str(boot_jdk.get('distribution', '')).strip())
        self.assertGreater(int(boot_jdk.get('size', 0)), 0)
        self.assertIs(boot_jdk_accepts(boot_jdk), True, 'a well-formed Linux x86_64 boot JDK must be accepted')
        windows_jdk = dict(boot_jdk)
        windows_jdk.update({'os': 'windows', 'arch': 'x86_64', 'file': 'OpenJDK21U-jdk_x64_windows_hotspot.zip'})
        self.assertIs(
            boot_jdk_accepts(windows_jdk), False,
            'a Windows boot-JDK artifact must fail closed (v6 acquires a Linux platform counterpart)',
        )
        missing_version = {key: value for key, value in boot_jdk.items() if key != 'java_version'}
        self.assertIs(boot_jdk_accepts(missing_version), False, 'an unobserved java -version must fail closed')

        # --- host tools: recorded command + observed output + PASS ----------- #
        host_tools = lock.get('host_tools')
        self.assertIsInstance(host_tools, dict)
        for tool in WSL_HOST_TOOLS:
            record = host_tools.get(tool)
            self.assertIsInstance(record, dict, f'missing host tool verification record: {tool}')
            self.assertEqual(record.get('result'), 'PASS', f'{tool} verification did not PASS')
            command = record.get('command')
            self.assertIsInstance(command, list)
            self.assertTrue(command)
            self.assertTrue(str(record.get('output', '')).strip(), f'{tool} recorded no observed output')
            _assert_absolute_posix(self, f'host tool {tool} path', record.get('path'))
        self.assertGreaterEqual(_parse_version(host_tools['make'].get('version')), (4, 0))
        for tool, record in host_tools.items():
            if isinstance(record, dict):
                self._assert_recorded_command_runs(distribution, tool, record)
        self.assertIn('py_compile', json.dumps(host_tools['python3']), 'python3 must py_compile the producer')

        # --- host package inventory ------------------------------------------ #
        host_packages = lock.get('host_packages')
        self.assertIsInstance(host_packages, dict)
        self.assertIsInstance(host_packages.get('pre_install'), list)
        self.assertIsInstance(host_packages.get('added'), list)

        # --- Linux workspace and build tree ---------------------------------- #
        workspace_root = _assert_absolute_posix(self, 'linux_workspace_root', lock.get('linux_workspace_root'))
        linux_tree = _assert_absolute_posix(self, 'linux_tree', lock.get('linux_tree'))
        self.assertFalse(workspace_root.startswith(MNT_PREFIX))
        self.assertFalse(linux_tree.startswith(MNT_PREFIX))
        probe = lock.get('workspace_probe')
        self.assertIsInstance(probe, dict)
        self.assertEqual(probe.get('symlink_round_trip'), 'PASS')
        self.assertEqual(probe.get('write_exec_delete'), 'PASS')
        self.assertIs(probe.get('on_mnt'), False)

        build_tree = lock.get('build_tree')
        self.assertIsInstance(build_tree, dict)
        self.assertEqual(build_tree.get('path'), linux_tree)
        self.assertEqual(build_tree.get('head'), SOURCE_COMMIT)
        self.assertEqual(build_tree.get('origin'), SOURCE_URL)
        self.assertIs(build_tree.get('commit_present'), True)
        self.assertIs(build_tree.get('tracked_tree_clean'), True)
        self.assertIs(build_tree.get('configure_executable'), True)
        self.assertIs(build_tree.get('pinned_surface_clean'), True)

        # raw wsl commands are recorded and re-measured at run time (M1)
        raw = build_tree.get('raw_commands')
        self.assertIsInstance(raw, list)
        self.assertTrue(raw, 'the raw wsl git invocations and their stdout must be recorded')
        self.assertTrue(all(distribution in json.dumps(entry) for entry in raw))
        self.assertEqual(_wsl_line(distribution, f'git -C {linux_tree} rev-parse HEAD'), SOURCE_COMMIT)
        self.assertEqual(
            _wsl(distribution, ['git', '-C', linux_tree, 'cat-file', '-e', f'{SOURCE_COMMIT}^{{commit}}']).returncode,
            0,
        )
        self.assertEqual(_wsl_line(distribution, f'git -C {linux_tree} status --porcelain -uno'), '')
        self.assertEqual(
            _wsl_line(distribution, f'git -C {linux_tree} status --porcelain -uno -- {" ".join(PINNED_SURFACE)}'),
            '',
        )
        self.assertEqual(
            _wsl_line(distribution, f'git -C {linux_tree} remote get-url origin'), SOURCE_URL,
        )
        self.assertEqual(_wsl_line(distribution, f'test -x {linux_tree}/configure && echo OK'), 'OK')

        # --- recorded artifacts still hash as recorded ----------------------- #
        self._assert_recorded_digests(distribution, lock)

        # --- receipted acquisition, including the failure path (M5) ---------- #
        self.assertTrue(receipt.get('commands'), 'the acquisition receipt must record every command executed')
        for entry in receipt.get('commands', []):
            self.assertIsInstance(entry, dict)
            self.assertTrue(entry.get('command'))
            self.assertIn('output', entry)
        for key in ('started_at_utc', 'completed_at_utc'):
            self.assertTrue(str(receipt.get(key, '')).strip())
        self.assertEqual(receipt.get('distro'), distribution)
        self.assertEqual(receipt.get('lock_written'), True)
        self._assert_acquisition_failure_receipt()

    # -- TEST-F106-04 (B1) / F106/U8: the transport carriers ----------------- #

    def test_f106_04_transport_integrity_three_carriers(self) -> None:
        """TEST-F106-04 "Transport integrity": three carriers, one constructor each.

        The assertion is the per-module identity check, not a shared module: no
        ``wsl_transport.py`` exists, no ``sys.path`` is mutated, and the loader is
        unchanged.  The canonical blocks are extracted **by symbol** and compared
        after normalising only the module-specific error class and the
        constructor's local argument-list expression.
        """
        for carrier in CARRIER_FILES:
            self.assertTrue(carrier.is_file(), f'missing F106 transport carrier: {carrier}')
        self.assertEqual(
            len({carrier.resolve() for carrier in CARRIER_FILES}), 3,
            'the three carriers must be three distinct files',
        )

        normalised: dict[str, str] = {}
        optional: dict[str, dict[str, str]] = {}
        for carrier in CARRIER_FILES:
            module = _load_module(f'f106_carrier_{carrier.stem}', carrier)
            text, tree = _carrier_source(carrier)
            symbols = _module_symbol_nodes(tree)
            for symbol in CANONICAL_TRANSPORT_SYMBOLS:
                self.assertIn(symbol, symbols, f'{carrier.name} must define the canonical symbol {symbol}')

            self.assertEqual(getattr(module, 'SCRIPT_TRANSPORT', None), 'base64-stdin')
            self.assertEqual(getattr(module, 'SCRIPT_DECODER_PIPELINE', None), CANONICAL_WSL_ARGUMENTS[2])

            argv, payload = module.wsl_script_invocation('Ubuntu-20.04', 'echo "$zzz" \\ \'x\'')
            self.assertEqual(argv[:4], ['wsl.exe', '-d', 'Ubuntu-20.04', '--'])
            # The card's §Interfaces :165 pins the seven-element argv explicitly.
            # Its :168 clause "argv[-2:] == ['bash','-lc']" contradicts both that
            # enumeration and its own neighbouring "argv[-1] == SCRIPT_DECODER_PIPELINE",
            # so it is unsatisfiable as written (the MEDIUM-2 defect class); the
            # enumeration and the argv[-1] clause are the consistent reading, and
            # all three carriers produce it.
            self.assertEqual(argv[4:], CANONICAL_WSL_ARGUMENTS)
            self.assertEqual(argv[-3:], CANONICAL_WSL_ARGUMENTS)
            self.assertEqual(argv[-3], 'bash')
            self.assertEqual(argv[-2], '-lc')
            self.assertEqual(argv[-1], module.SCRIPT_DECODER_PIPELINE)
            self.assertNotIn('\n', payload)
            self.assertNotIn('\r', payload)
            self.assertEqual(base64.b64decode(payload).decode('utf-8'), 'echo "$zzz" \\ \'x\'')

            # "where present": the module-specific symbols are value-asserted, and
            # their source blocks are compared across whichever carriers carry them
            for symbol in PRESENT_IF_DEFINED_TRANSPORT_SYMBOLS:
                block = _present_symbol_block(text, tree, symbol)
                if block is None:
                    continue
                optional.setdefault(symbol, {})[carrier.name] = block
                if symbol == 'WSL_SCRIPT_ARGUMENTS':
                    self.assertEqual(getattr(module, symbol, None), CANONICAL_WSL_ARGUMENTS)

            # no `bash -lc <script>` argv construction outside the constructor
            offenders = _interpolated_script_argv_sites(tree, symbols['wsl_script_invocation'], symbols)
            self.assertEqual(offenders, [], f'{carrier.name} still constructs a bash -lc <script> argv: {offenders}')

            normalised[carrier.name] = _normalised_canonical_blocks(text, tree, module)

        reference = CARRIER_FILES[0].name
        for name, block in normalised.items():
            self.assertEqual(
                block, normalised[reference],
                f'the transport carrier {name} carries a DIVERGENT copy of {reference}\'s canonical blocks',
            )
        for symbol, blocks in optional.items():
            # A module-specific symbol may legitimately exist in one carrier only
            # (MANIFEST_HEREDOC is openjdk_ohos.py's manifest staging token), so
            # the corpus narrowing is forced by the working tree, not a weakening.
            first_name = next(iter(blocks))
            for name, block in blocks.items():
                self.assertEqual(block, blocks[first_name], f'{name} carries a divergent {symbol} block')
        self.assertIn(
            'WSL_SCRIPT_ARGUMENTS', optional,
            'WSL_SCRIPT_ARGUMENTS must exist in at least one carrier (the two production modules and this file)',
        )

        # F106/U8: this file's own helpers must route through its LOCAL constructor
        own_text, own_tree = _carrier_source(Path(__file__).resolve())
        own_symbols = _module_symbol_nodes(own_tree)
        line = own_symbols['_wsl_line']
        relay = own_symbols['_wsl_script']
        self.assertIn('wsl_script_invocation', _called_names(relay),
                      '_wsl_script must route through the local wsl_script_invocation()')
        self.assertIn('_wsl_script', _called_names(line), '_wsl_line must route through _wsl_script()')
        self.assertIn('_wsl', _called_names(relay), '_wsl_script must launch through the local _wsl()')

    def test_f106_04_transport_fourth_carrier_scan(self) -> None:
        """TEST-F106-04 (B1) / F106 LOW-1: the fourth-carrier scan is pinned, not judged."""
        carriers = {carrier.resolve() for carrier in CARRIER_FILES}
        for root in CARRIER_SCAN_ROOTS:
            self.assertTrue(root.is_dir(), f'missing pinned F106 carrier scan root: {root}')
        # the scan must not be vacuous: every carrier lies inside a pinned root
        for carrier in CARRIER_FILES:
            self.assertTrue(
                any(root.resolve() in carrier.resolve().parents for root in CARRIER_SCAN_ROOTS),
                f'{carrier} is outside the pinned scan roots, so the scan would not see it',
            )
        scanned = [path for root in CARRIER_SCAN_ROOTS for path in root.rglob('*')
                   if path.is_file() and path.suffix in CARRIER_SCAN_KINDS
                   and not any(part in CARRIER_SCAN_EXCLUDED_DIRS for part in path.parts)]
        self.assertTrue(scanned, 'the pinned scan scope contains no source file: the scan would pin nothing')

        found = _carrier_shaped_files(CARRIER_SCAN_ROOTS, CARRIER_SCAN_EXCLUDED_DIRS, carriers)
        self.assertEqual(
            [path.as_posix() for path in found], [],
            'a fourth transport carrier exists in the pinned scan scope (a wsl.exe argv carrying the decoder pipeline)',
        )
        self.assertEqual(
            _excluded_source_files(CARRIER_SCAN_ROOTS, CARRIER_SCAN_EXCLUDED_DIRS), [],
            'the exclusion list holds a source file, so it could hide a carrier',
        )

        # anti-false-positive rule: a wsl.exe invocation WITHOUT the decoder pipeline must not flag
        benign = ROOT / 'scripts' / 'run_ohos_generic_acceptance.sh'
        if benign.is_file():
            benign_text = benign.read_text(encoding='utf-8', errors='replace')
            self.assertIn('wsl.exe', benign_text, 'the anti-false-positive control must exercise a real wsl.exe site')
            self.assertNotIn(CARRIER_DETECTION_LITERAL, benign_text)
            self.assertNotIn(benign.resolve(), {path.resolve() for path in found})

    def test_f106_04_transport_negative_controls(self) -> None:
        """The identity assertion and the scan must FAIL for the reason each names.

        CODE-011: a negative rejected for a reason other than the one it names
        pins nothing, so each control asserts the failing dimension.
        """
        carrier = CARRIER_FILES[1]
        module = _load_module('f106_carrier_identity_reference', carrier)
        text, tree = _carrier_source(carrier)
        reference = _normalised_canonical_blocks(text, tree, module)

        # (1) a divergent constructor argument-list VALUE is refused, and refused
        #     by the value assertion -- not by an incidental text mismatch.
        mutated_text = text.replace('*WSL_SCRIPT_ARGUMENTS', '"bash", "-c", SCRIPT_DECODER_PIPELINE')
        self.assertNotEqual(mutated_text, text, 'the argument-list mutation did not reach the source')
        with self.assertRaises(AssertionError) as context:
            _normalise_constructor(
                mutated_text,
                _module_symbol_nodes(ast.parse(mutated_text))['wsl_script_invocation'],
                module,
            )
        self.assertIn('not the canonical', str(context.exception), str(context.exception))

        # (2) a divergent canonical block in ANY carrier is detected as divergence.
        for other in CARRIER_FILES:
            other_text, other_tree = _carrier_source(other)
            other_module = _load_module(f'f106_carrier_negative_{other.stem}', other)
            other_reference = _normalised_canonical_blocks(other_text, other_tree, other_module)
            for symbol, replacement in (
                ('SCRIPT_TRANSPORT', '"base64-file"'),
                ('SCRIPT_DECODER_PIPELINE', '"base64 -d | bash"'),
            ):
                mutated = re.sub(
                    rf'^{symbol} = .*$', f'{symbol} = {replacement}', other_text, count=1, flags=re.MULTILINE,
                )
                self.assertNotEqual(mutated, other_text, f'the {symbol} mutation did not reach {other.name}')
                diverged = _normalised_canonical_blocks(mutated, ast.parse(mutated), other_module)
                self.assertNotEqual(
                    diverged, other_reference,
                    f'a divergent {symbol} in {other.name} was not detected',
                )
            # a module-specific symbol is caught by the "where present" comparator
            optional_reference = _present_symbol_block(other_text, other_tree, 'WSL_SCRIPT_ARGUMENTS')
            if optional_reference is not None:
                mutated = re.sub(
                    r'^WSL_SCRIPT_ARGUMENTS = .*$',
                    'WSL_SCRIPT_ARGUMENTS = ["bash", "-c", SCRIPT_DECODER_PIPELINE]',
                    other_text, count=1, flags=re.MULTILINE,
                )
                self.assertNotEqual(mutated, other_text)
                self.assertNotEqual(
                    _present_symbol_block(mutated, ast.parse(mutated), 'WSL_SCRIPT_ARGUMENTS'), optional_reference,
                    f'a divergent WSL_SCRIPT_ARGUMENTS in {other.name} was not detected',
                )

        # (3) the interpolated-argv detector catches the PRE-REPAIR shape and does
        #     not flag the compliant shape (the hazard-matched control).
        caught = _interpolated_script_argv_sites(
            ast.parse("LOCAL = 'x'\nargv = ['bash', '-lc', script]\n"),
            None,
            _module_symbol_nodes(ast.parse("LOCAL = 'x'\nargv = ['bash', '-lc', script]\n")),
        )
        self.assertTrue(caught, 'a local variable in the -lc argv position must be caught')
        clean_source = 'SCRIPT_DECODER_PIPELINE = "base64 -d | bash -l"\nARGV = ["bash", "-lc", SCRIPT_DECODER_PIPELINE]\n'
        self.assertEqual(
            _interpolated_script_argv_sites(ast.parse(clean_source), None, _module_symbol_nodes(ast.parse(clean_source))),
            [],
            'the compliant constant-form argv list must not be flagged',
        )

        # (4) the fourth-carrier detector flags a divergent carrier-shaped copy and
        #     leaves a pipeline-free wsl.exe site alone.
        probe_root = Path(tempfile.mkdtemp(prefix='f106-u8-scan-'))
        self.addCleanup(shutil.rmtree, probe_root, True)
        (probe_root / 'divergent_carrier.py').write_text(
            'def wsl_script_invocation(d, s):\n'
            '    return ["wsl.exe", "-d", d, "--", "bash", "-lc", "base64 -d | bash -l"], s\n',
            encoding='utf-8',
        )
        (probe_root / 'benign_user.sh').write_text(
            'MSYS2_ARG_CONV_EXCL=\'*\' wsl.exe -d Ubuntu-20.04 -- uname -s\n', encoding='utf-8',
        )
        flagged = {path.name for path in _carrier_shaped_files((probe_root,), CARRIER_SCAN_EXCLUDED_DIRS, set())}
        self.assertEqual(flagged, {'divergent_carrier.py'},
                         f'the scan must flag exactly the carrier-shaped copy, got {sorted(flagged)}')

    # -- TEST-F106-01 / TEST-F106-04 (H1): the AD-2 fixtures ------------------- #

    def test_f106_01_disposition_precedence_fixtures(self) -> None:
        """TEST-F106-01 "Disposition precedence": fixtures (a)-(e) under the candidate unit.

        The expected outcomes are first derived from the pinned AD-2 order by an
        in-test model, so the fixture directions are checkable against the card's
        rules rather than against the card's fixture prose.  The two directions the
        v10 review adjudicated are asserted here:
          * MEDIUM-2 -- (e) is a disjoint clang-vs-apiVersion tie and AD-2 puts
            apiVersion BEFORE clang, so apiVersion wins -> branch 1;
          * LOW-1 -- (b)'s reverse-order control flips the branch to 1.
        """
        for fixture in AD2_FIXTURES:
            examined = list(fixture['candidates'])
            counts = _ad2_counts(examined)
            self.assertEqual(counts, fixture['counts'], f"{fixture['case']}: candidate-unit counts")
            self.assertEqual(_ad2_stop_branch(counts, None, examined), fixture['branch'], f"{fixture['case']}: branch")
            # The reverse-order control is asserted only where the fixture names it:
            # (b) and (e) are the two fixtures whose OUTCOME discriminates the order.
            if 'reverse_counts' in fixture:
                reverse_counts = _ad2_counts(examined, REVERSED_AD2_ORDER)
                self.assertEqual(reverse_counts, fixture['reverse_counts'], f"{fixture['case']}: reverse-order counts")
                self.assertEqual(
                    _ad2_stop_branch(reverse_counts, None, examined, REVERSED_AD2_ORDER),
                    fixture['reverse_branch'],
                    f"{fixture['case']}: reverse-order branch",
                )
        self.assertEqual(
            _ad2_stop_branch({}, None, []), '1_no_admissible_sdk',
            'a route that examined nothing stops on branch 1, never branch 2 (OQ-9 Condition 2)',
        )

        # the production seam: the same fixtures, driven through admissible_candidate()
        record = _load(DECISION_RECORD)
        runner = _load_module('f106_o_runner_fixtures', O_RUNNER)
        admissible = _require_callable(self, runner, 'admissible_candidate', O_RUNNER)
        for fixture in AD2_FIXTURES:
            for candidate in fixture['candidates']:
                observed = _fixture_observed(record, candidate['failed'])
                result = admissible(observed, record)
                self.assertEqual(
                    result.get('disposition'), _ad2_first_match(candidate['failed']),
                    f"{fixture['case']}/{candidate['id']}: first-match disposition",
                )
                self.assertEqual(
                    result.get('rejection_counts'),
                    {_ad2_first_match(candidate['failed']): 1},
                    f"{fixture['case']}/{candidate['id']}: one count per examined candidate",
                )
                recorded = {entry.get('name') for entry in result.get('deltas') or []}
                for dimension in candidate['failed']:
                    self.assertIn(
                        dimension, recorded,
                        f"{fixture['case']}/{candidate['id']}: the disposition is a label, not a filter -- "
                        'every failed dimension must still be recorded',
                    )

    def test_f106_04_admissibility_lattice_fixtures(self) -> None:
        """TEST-F106-04 (i)-(v): the AD-2 lattice, driven through the pure seam."""
        record = _load(DECISION_RECORD)
        runner = _load_module('f106_o_runner_lattice', O_RUNNER)
        producer = _load_module('f106_k_producer_lattice', PRODUCER)
        runner_seam = _require_callable(self, runner, 'admissible_candidate', O_RUNNER)
        producer_seam = _require_callable(self, producer, 'admissible_candidate', PRODUCER)
        _require_callable(self, runner, 'deviation_consistency', O_RUNNER)
        _require_callable(self, producer, 'deviation_consistency', PRODUCER)

        verdicts: dict[str, Any] = {}
        for vector in AD2_LATTICE_VECTORS:
            observed = _fixture_observed(record, vector['failed'])
            for label, seam in (('O runner', runner_seam), ('K producer', producer_seam)):
                result = seam(observed, record)
                self.assertEqual(
                    result.get('disposition'), vector['disposition'],
                    f"{vector['case']} ({label}): {vector['reason']}",
                )
                self.assertEqual(result.get('adoption_mode'), vector['adoption_mode'], f"{vector['case']} ({label})")
                verdicts[f"{vector['case']}/{label}"] = result
        # prefer_frozen_exact: whenever both tiers are examined, Tier A is adopted
        self.assertTrue(verdicts, 'the lattice vectors must produce verdicts')
        for label in ('O runner', 'K producer'):
            self.assertEqual(
                verdicts[f'(ii)/{label}'].get('adoption_mode'), 'version_deviation',
                f'{label}: a conformant version-deviation candidate is adopted (branch 3)',
            )
            self.assertEqual(
                verdicts[f'(iii)/{label}'].get('adoption_mode'), 'frozen_exact',
                f'{label}: a pins-conformant frozen-version candidate is admissible AND preferred',
            )
            self.assertIsNone(verdicts[f'(iii)/{label}'].get('sdk_version_deviation'), f'{label}: frozen_exact carries no deviation')
        # the AD-2 completeness assertion: exactly one entry per declared pin, by name and length
        for vector in AD2_LATTICE_VECTORS:
            observed = _fixture_observed(record, vector['failed'])
            names = {entry.get('name') for entry in observed['dimensions']}
            self.assertEqual(names, set(DECLARED_PINS), f"the fixture {vector['case']} must carry the closed pin set")

    def test_f106_04_widening_contract(self) -> None:
        """TEST-F106-04 "Widening contract" (F106/U0, M1, HIGH-1): asserted before any consumer runs."""
        self.assertTrue(
            DECISION_RECORD.is_file(),
            f'the F106 decision record is a predecessor artifact of every consumer: {DECISION_RECORD}',
        )
        record = _load(DECISION_RECORD)
        self.assertTrue(
            DECISION_RECORD_PRE_WIDENING_ARCHIVE.is_file(),
            'F106/U0 must copy the pre-widening record to the named archive BEFORE the widening writes '
            f'(the immutability operand, LOW-2): {DECISION_RECORD_PRE_WIDENING_ARCHIVE}',
        )
        pre = _load(DECISION_RECORD_PRE_WIDENING_ARCHIVE)
        self.assertTrue(DECISION_RECORD_GENERATOR.is_file(), f'missing widening generator: {DECISION_RECORD_GENERATOR}')

        # --- the widening marker, and its own pre-consumer digest --------------- #
        schema = str(record.get('schema', ''))
        self.assertTrue(schema, 'the widened record must carry a schema marker')
        self.assertNotEqual(schema, str(pre.get('schema', '')), 'the widened marker must be distinct from the pre-widening shape')
        self.assertIn(DECISION_RECORD_WIDENED_MARKER, schema.lower())
        self.assertTrue(str(record.get('widened_at_utc', '')).strip(), 'the widening marker must record widened_at_utc')
        recomputed = _canonical_lock_sha256(record, list(DECISION_RECORD_MARKER_EXCLUDED_FIELDS))
        self.assertEqual(
            _assert_hex64(self, record.get(DECISION_RECORD_MARKER_DIGEST_FIELD)), recomputed,
            "the marker's canonical_content_sha256 must match a recomputation over the file's canonical "
            'content with the digest-bearing fields excluded',
        )

        # --- the identity join (HIGH-1): one bucket-key definition, no second vocabulary
        bundles = record.get('bundles')
        linux_sdk = record.get('linux_sdk')
        self.assertIsInstance(bundles, dict, 'the widened record must carry a bundles block')
        self.assertIsInstance(linux_sdk, dict)
        self.assertTrue(bundles, 'bundles must be non-empty')
        self.assertEqual(
            set(bundles), set(linux_sdk),
            'a bundles key IS the declared-version key of the linux_sdk entry (the identity join)',
        )
        self.assertEqual(
            int(record.get('adopted_bundle_count', -1)), len(bundles),
            'the widening marker must record the adopted-bundle count',
        )
        for version, bucket in bundles.items():
            self.assertIsInstance(bucket, dict, f'bundles[{version}] must be a bucket')
            self.assertEqual(
                set(bucket), {'url', 'http_status', 'size', 'sha256', 'stream', 'members', 'probed_at_utc'},
                f'bundles[{version}] carries a key outside the pinned bucket schema',
            )
            self.assertIsInstance(bucket.get('members'), dict)
            self.assertEqual(set(bucket['members']), BUNDLE_MEMBER_SUBKEYS, f'bundles[{version}].members sub-keys')
            self.assertEqual(linux_sdk[version].get('bundle_ref'), version, f'linux_sdk[{version}].bundle_ref')
            self.assertEqual(
                bucket.get('stream'), pre['linux_sdk'][version].get('stream'),
                f'linux_sdk[{version}].stream is never rewritten by the widening (it is carried as a value)',
            )
            member = linux_sdk[version].get('bundle_member')
            self.assertIsInstance(member, str)
            self.assertIn('/', str(member), f'linux_sdk[{version}].bundle_member must be the full in-tar member path')
            self.assertEqual(
                member, pre['linux_sdk'][version].get('bundle_member'),
                f'linux_sdk[{version}].bundle_member: the normalisation is a statement of form, not a change',
            )

        # --- availability, and the renamed frozen_pins keys -------------------- #
        availability = record.get('availability')
        self.assertIsInstance(availability, list)
        self.assertTrue(availability, 'availability[] must be present and non-empty')
        for entry in availability:
            self.assertIsInstance(entry, dict)
            for key in ('url', 'status', 'note'):
                self.assertIn(key, entry, f'availability[] entry is missing {key}')
        renamed = {
            'windows_native_oh_uni_package_json_sha256': 'windows_native_package_sha256',
            'windows_toolchains_oh_uni_package_json_sha256': 'windows_toolchains_package_sha256',
        }
        for name, previous in renamed.items():
            self.assertIn(name, record['frozen_pins'], f'the renamed frozen_pins key {name} must be present')
            self.assertEqual(
                record['frozen_pins'][name], pre['frozen_pins'][previous],
                f'the rename carries the value: {previous} -> {name}',
            )
        for name in ('sdk_api', 'sdk_version', 'clang_revision', 'clang_version_string'):
            self.assertIn(name, record['frozen_pins'], f'the frozen_pins key {name} must survive the widening')

        # --- additive-only: no pre-existing value is rewritten ------------------ #
        # The freeze is scoped exactly as LOW-2 adjudicated: bundle_ref is added,
        # bundle_member is EXPRESSED as the full in-tar path it already carries,
        # the renamed frozen_pins keys carry their values, and
        # linux_sdk.<ver>.stream is never normalised, re-spelled or moved.
        exempt_leaf = set(renamed.values()) | {'bundle_member', 'bundle_ref'}
        for path, value in _leaf_items(pre):
            if path[-1] in exempt_leaf:
                continue
            observed_value = _lookup(record, path)
            self.assertIsNot(
                observed_value, _MISSING,
                f'the widening dropped the pre-existing value at {path}',
            )
            self.assertEqual(observed_value, value, f'the widening rewrote the pre-existing value at {path}')

    def test_f106_01_verdict_token_sweep(self) -> None:
        """TEST-F106-01 "Verdict-token sweep, hazard-driven" (M2 / AD-7).

        Every verdict is derived by closed-set membership on an exact token in
        the form the site's hazard requires.  At the policy site the hazard is a
        token appearing as a WORD INSIDE BENIGN TEXT, so whole-line or field-exact
        matching is required and word-boundary matching is **not** admissible --
        because a benign log line matching a marker would remove a real failure
        from the failure budget as INFRASTRUCTURE_PERMISSION_FAILURE (AGENTS.md §7).
        """
        acquisition = _load_module('f106_wsl_verdicts', WSL_ACQUIRE)
        is_policy_failure = _require_callable(self, acquisition, '_is_policy_failure', WSL_ACQUIRE)
        markers = getattr(acquisition, 'POLICY_MARKERS', None)
        self.assertIsInstance(markers, (tuple, list, set), 'POLICY_MARKERS must be a module-level closed set')
        self.assertTrue(markers)

        for marker in sorted(str(entry) for entry in markers):
            # polarity: the token in the form the hazard permits is accepted
            self.assertIs(
                bool(is_policy_failure(marker)), True,
                f'the policy verdict must be satisfied by the marker {marker!r}',
            )
            self.assertIs(
                bool(is_policy_failure(marker.upper())), True,
                f'the policy verdict must be case-insensitive for {marker!r}',
            )
            # benign-lookalike control: the marker inside benign prose must NOT satisfy it
            for benign in (
                f'the run log states that no {marker} was observed during the probe\n',
                f'note: we saw {marker} here, but it was not a host block',
            ):
                self.assertIs(
                    bool(is_policy_failure(benign)), False,
                    'a benign-lookalike line carrying the marker as a word inside prose must not satisfy the '
                    f'policy verdict; word-boundary matching is not admissible at this site: {benign!r}',
                )

        # the `wsl -l -v` row decode: an exact distro-name token, never a substring,
        # and the header must not become a row (a header/decoy control).
        listing = (
            '  NAME            STATE           VERSION\n'
            '* Ubuntu-20.04    Running         2\n'
            '  Ubuntu-22.04    Stopped         1\n'
        )
        row = acquisition._distribution_row(listing, 'Ubuntu-20.04')
        self.assertIsInstance(row, dict)
        self.assertEqual(row.get('name'), 'Ubuntu-20.04')
        self.assertEqual(str(row.get('version')), '2')
        self.assertIsNone(
            acquisition._distribution_row(listing, 'Ubuntu'),
            'the row decode must match an exact distro-name token, never a substring',
        )
        for decoy in ('NAME', 'VERSION', 'STATE'):
            self.assertIsNone(
                acquisition._distribution_row(listing, decoy),
                f'the header/decoy token {decoy!r} must not decode as a distribution row',
            )
        self.assertEqual(acquisition._parse_index_version(listing, 'Ubuntu-22.04'), 1)
        with self.assertRaises(Exception):
            acquisition._parse_index_version(listing, 'Ubuntu')

    # -- helpers used by TEST-F106-04 --------------------------------------- #

    def _assert_recorded_command_runs(self, distribution: str, tool: str, record: dict[str, Any]) -> None:
        command = [str(part) for part in record.get('command', [])]
        forbidden = {'rm', 'mv', 'dd', 'mkfs', 'chmod', 'chown', 'sudo', 'shutdown', 'reboot'}
        if not command or Path(command[0]).name in forbidden:
            return
        script = ' '.join(_shell_quote(part) for part in command)
        completed = _wsl_script(distribution, f"cd {record.get('cwd', '/')} && {script}", timeout=120)
        self.assertEqual(completed.returncode, 0, f'{tool} recorded command no longer runs:\n{completed.stdout}')

    def _assert_recorded_digests(self, distribution: str, lock: dict[str, Any]) -> None:
        entries = _recorded_artifact_digests(lock)
        if not entries:
            return
        script = 'sha256sum ' + ' '.join(_shell_quote(path) for _, path, _ in entries)
        completed = _wsl_script(distribution, script, timeout=180)
        observed = {}
        for line in completed.stdout.splitlines():
            fields = line.split()
            if len(fields) == 2:
                observed[fields[1]] = fields[0].upper()
        for label, path, digest in entries:
            self.assertIn(path, observed, f'{label} could not be re-hashed on the build host:\n{completed.stdout}')
            self.assertEqual(observed[path], digest, f'{label} changed on disk since acquisition')

    def _assert_acquisition_failure_receipt(self) -> None:
        """M5: the acquisition writes its receipt even when it fails."""
        base = Path(tempfile.mkdtemp(prefix='f106-v6-acquire-'))
        self.addCleanup(shutil.rmtree, base, True)
        receipt_path = base / 'receipt.json'
        lock_path = base / 'lock.json'
        completed = subprocess.run(
            [
                sys.executable,
                str(WSL_ACQUIRE),
                '--distro',
                f'mdds-f106-absent-{os.getpid()}',
                '--base',
                str(base / 'work'),
                '--lock',
                str(lock_path),
                '--receipt',
                str(receipt_path),
            ],
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=300,
            check=False,
        )
        self.assertNotEqual(completed.returncode, 0, f'a missing distribution must not succeed:\n{completed.stdout}')
        self.assertTrue(receipt_path.is_file(), 'the acquisition receipt must be written even on failure (M5/C12)')
        failure = _load(receipt_path)
        self.assertEqual(failure.get('schema'), WSL_RECEIPT_SCHEMA)
        self.assertIn(failure.get('result'), ACQUIRE_FAILURE_RESULTS)
        self.assertFalse(lock_path.exists(), 'a failed acquisition must not write the lock')
        commands = failure.get('commands')
        self.assertIsInstance(commands, list)
        self.assertTrue(commands, 'the failure receipt must record the commands that were executed')
        for entry in commands:
            self.assertIsInstance(entry, dict)
            self.assertTrue(entry.get('command'))
            self.assertIn('output', entry)
        self.assertTrue(str(failure.get('non_finding', '')).strip(), 'the failure receipt must state an explicit non-finding')
        self.assertTrue(failure.get('probes'), 'the failure receipt must record what was probed')
        self.assertIn('mdds-f106-absent', json.dumps(failure))


def _carrier_source(path: Path) -> tuple[str, ast.Module]:
    """Read a carrier with universal newlines, so a CRLF checkout is not a false divergence."""
    text = path.read_text(encoding='utf-8')
    return text, ast.parse(text, filename=str(path))


def _module_symbol_nodes(tree: ast.Module) -> dict[str, ast.AST]:
    symbols: dict[str, ast.AST] = {}
    for node in tree.body:
        names: list[str] = []
        if isinstance(node, ast.Assign):
            names = [target.id for target in node.targets if isinstance(target, ast.Name)]
        elif isinstance(node, ast.AnnAssign) and isinstance(node.target, ast.Name):
            names = [node.target.id]
        elif isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            names = [node.name]
        for name in names:
            symbols.setdefault(name, node)
    return symbols


def _plain_string_constant(node: ast.AST | None) -> str | None:
    """The value of a module-level ``NAME = "<plain string>"`` assignment, or None."""
    if not isinstance(node, ast.Assign) or len(node.targets) != 1:
        return None
    value = node.value
    if isinstance(value, ast.Constant) and isinstance(value.value, str):
        return value.value
    return None


def _segment(text: str, node: ast.AST) -> str:
    segment = ast.get_source_segment(text, node)
    if segment is None:
        raise AssertionError(f'the canonical transport block at line {node.lineno} could not be extracted')
    return segment


def _encloses(outer: ast.AST, inner: ast.AST) -> bool:
    return (
        (outer.lineno, outer.col_offset) <= (inner.lineno, inner.col_offset)
        and (inner.end_lineno, inner.end_col_offset) <= (outer.end_lineno, outer.end_col_offset)
    )


def _arguments_tail(argv: ast.List) -> list[ast.AST]:
    for index, element in enumerate(argv.elts):
        if isinstance(element, ast.Constant) and element.value == '--':
            tail = argv.elts[index + 1:]
            if tail:
                return tail
    raise AssertionError('the transport constructor does not separate its argv at "--"')


def _resolve_arguments(tail: list[ast.AST], module: Any) -> list[Any]:
    resolved: list[Any] = []
    for element in tail:
        if isinstance(element, ast.Starred):
            target = element.value
            if not isinstance(target, ast.Name):
                raise AssertionError('the transport constructor splats a non-name expression')
            value = getattr(module, target.id, None)
            if not isinstance(value, list):
                raise AssertionError(f'the splatted {target.id} must be a list constant')
            resolved.extend(value)
        elif isinstance(element, ast.Constant):
            resolved.append(element.value)
        elif isinstance(element, ast.Name):
            resolved.append(getattr(module, element.id, None))
        else:
            raise AssertionError(f'unsupported constructor argument expression: {ast.dump(element)}')
    return resolved


def _normalise_constructor(text: str, function: ast.FunctionDef, module: Any) -> str:
    """Canonical block for the constructor, normalising exactly two things.

    (i) the module-specific error-class identifier, and (ii) the constructor's
    local argument-list expression -- whose *value* is asserted first, so the
    placeholder never hides a genuinely different argv.
    """
    returns = [node for node in ast.walk(function) if isinstance(node, ast.Return)]
    if len(returns) != 1:
        raise AssertionError('the transport constructor must contain exactly one return statement')
    value = returns[0].value
    if not isinstance(value, ast.Tuple) or len(value.elts) != 2 or not isinstance(value.elts[0], ast.List):
        raise AssertionError('the transport constructor must return the (argv, stdin) pair')
    argv = value.elts[0]
    tail = _arguments_tail(argv)
    resolved = _resolve_arguments(tail, module)
    if resolved != CANONICAL_WSL_ARGUMENTS:
        raise AssertionError(
            'the transport constructor argument list is not the canonical '
            f'{CANONICAL_WSL_ARGUMENTS!r}: {resolved!r}'
        )
    lines = _segment(text, function).split('\n')
    start = tail[0].lineno - function.lineno
    end = tail[-1].end_lineno - function.lineno
    head = lines[start].encode('utf-8')
    foot = lines[end].encode('utf-8')
    lines[start:end + 1] = [
        (head[:tail[0].col_offset] + ARGS_EXPRESSION_PLACEHOLDER.encode('utf-8') + foot[tail[-1].end_col_offset:]).decode('utf-8')
    ]
    return _carrier_error_class_re().sub('ERRCLASS', '\n'.join(lines))


def _carrier_error_class_re() -> re.Pattern[str]:
    return re.compile(r'\b(' + '|'.join(CARRIER_ERROR_CLASSES) + r')\b')


def _normalised_canonical_blocks(
    text: str,
    tree: ast.Module,
    module: Any,
    corpus: tuple[str, ...] = CANONICAL_TRANSPORT_SYMBOLS,
) -> str:
    """The canonical blocks for ``corpus``, extracted by symbol and normalised twice.

    The corpus is a closed set of symbols that exist in **every** carrier; the
    module-specific symbols (``WSL_SCRIPT_ARGUMENTS``, ``MANIFEST_HEREDOC``) are
    legitimately present in one or two carriers only and are compared separately,
    so a literal fixed-length corpus could never go GREEN -- the same
    "requirement that cannot be satisfied" class the v8 B1 shared-module
    requirement was declined for.
    """
    symbols = _module_symbol_nodes(tree)
    blocks: dict[str, str] = {}
    for symbol in corpus:
        node = symbols.get(symbol)
        if node is None:
            raise AssertionError(f'the carrier does not define the canonical symbol {symbol}')
        if isinstance(node, ast.FunctionDef):
            blocks[symbol] = _normalise_constructor(text, node, module)
        else:
            blocks[symbol] = _carrier_error_class_re().sub('ERRCLASS', _segment(text, node))
    return '\n'.join(f'--- {symbol} ---\n{blocks[symbol]}' for symbol in sorted(blocks))


def _present_symbol_block(text: str, tree: ast.Module, symbol: str) -> str | None:
    """The canonical block for an optional symbol, or None when the carrier omits it."""
    node = _module_symbol_nodes(tree).get(symbol)
    if node is None:
        return None
    return _carrier_error_class_re().sub('ERRCLASS', _segment(text, node))


def _called_names(function: ast.AST) -> set[str]:
    return {
        node.func.id
        for node in ast.walk(function)
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
    }


def _interpolated_script_argv_sites(tree: ast.Module, exempt: ast.AST | None, symbols: dict[str, ast.AST]) -> list[str]:
    """``bash -lc <script>`` argv construction outside the transport constructor.

    A ``-lc``-bearing argv list may only carry plain string constants and names
    that resolve to module-level plain string constants.  A local variable, an
    f-string, a call or a subscript in that position *is* interpolated script
    text -- the CODE-008 defect class -- and is reported.
    """
    offenders: list[str] = []
    for node in ast.walk(tree):
        if not isinstance(node, (ast.List, ast.Tuple)):
            continue
        if exempt is not None and _encloses(exempt, node):
            continue
        if not any(isinstance(element, ast.Constant) and element.value == '-lc' for element in node.elts):
            continue
        for element in node.elts:
            if isinstance(element, ast.Constant):
                if isinstance(element.value, str) and any(token in element.value for token in PLAIN_ARGV_FORBIDDEN):
                    offenders.append(f'line {node.lineno}: quoting/expansion token inside argv word {element.value!r}')
                    break
            elif isinstance(element, ast.Name):
                if _plain_string_constant(symbols.get(element.id)) is None:
                    offenders.append(f'line {node.lineno}: argv word {element.id} is not a module-level string constant')
                    break
            else:
                offenders.append(f'line {node.lineno}: interpolated argv word {ast.dump(element)[:80]}')
                break
    return offenders


def _carrier_shaped_files(roots: tuple[Path, ...], excluded_dirs: tuple[str, ...], carriers: set[Path]) -> list[Path]:
    found: list[Path] = []
    for root in roots:
        for path in sorted(root.rglob('*')):
            if not path.is_file() or path.suffix not in CARRIER_SCAN_KINDS:
                continue
            if any(part in excluded_dirs for part in path.parts):
                continue
            if path.resolve() in carriers:
                continue
            if CARRIER_DETECTION_LITERAL in path.read_text(encoding='utf-8', errors='replace'):
                found.append(path)
    return found


def _excluded_source_files(roots: tuple[Path, ...], excluded_dirs: tuple[str, ...]) -> list[Path]:
    """Source files inside the exclusion list: must be empty, or the list hides a carrier."""
    return [
        path
        for root in roots
        for path in sorted(root.rglob('*'))
        if path.is_file()
        and path.suffix in CARRIER_SCAN_KINDS
        and any(part in excluded_dirs for part in path.parts)
    ]


def _ad2_first_match(failed_dimensions: Any, order: tuple[str, ...] = AD2_DISPOSITION_ORDER) -> str:
    """AD-2: the disposition labelling a candidate is the first match of the total order."""
    labels = {_F106_DIMENSION_DISPOSITION[name] for name in failed_dimensions}
    for disposition in order:
        if disposition in labels:
            return disposition
    raise AssertionError(f'the fixture failed dimensions {sorted(failed_dimensions)} yield no disposition')


def _ad2_counts(candidates: Any, order: tuple[str, ...] = AD2_DISPOSITION_ORDER) -> dict[str, int]:
    """AD-2 MEDIUM-1: one count per examined candidate, keyed by its first-match disposition."""
    counts: dict[str, int] = {}
    for candidate in candidates:
        disposition = _ad2_first_match(candidate['failed'], order)
        counts[disposition] = counts.get(disposition, 0) + 1
    return counts


def _ad2_dominant(counts: dict[str, int], order: tuple[str, ...] = AD2_DISPOSITION_ORDER) -> str:
    ranked = sorted(counts.items(), key=lambda item: (-item[1], order.index(item[0]) if item[0] in order else len(order)))
    return ranked[0][0]


def _ad2_stop_branch(
    counts: dict[str, int],
    adopted_mode: str | None,
    examined: Any,
    order: tuple[str, ...] = AD2_DISPOSITION_ORDER,
) -> str:
    if adopted_mode == 'frozen_exact':
        return 'none'
    if adopted_mode == 'version_deviation':
        return '3_version_label_accepted'
    if not examined:
        return '1_no_admissible_sdk'
    dominant = _ad2_dominant(counts, order)
    clang_examined = any(_ad2_first_match(candidate['failed'], order) == 'rejected_clang_revision' for candidate in examined)
    if dominant == 'rejected_clang_revision' and clang_examined:
        return '2_clang_revision_mismatch'
    return '1_no_admissible_sdk'


_F106_DIMENSION_DISPOSITION = {
    'unavailable': 'unavailable',
    'api_version': 'rejected_api_version',
    'clang_revision_string': 'rejected_clang_revision',
    'linux_clang_binary': 'rejected_artifact_pin',
    'ohos_toolchain_cmake': 'rejected_artifact_pin',
    'target_libc': 'rejected_artifact_pin',
    'native_sdk_package_archive': 'rejected_artifact_pin',
    'toolchains_sdk_package_archive': 'rejected_artifact_pin',
    'unrecorded_version': 'rejected_unrecorded_version',
}

# TEST-F106-01 "Disposition precedence" -- the five fixtures (a)-(e), with the
# counting unit pinned to ONE COUNT PER EXAMINED CANDIDATE (AD-2, the v9 review's
# MEDIUM-1).  Two directions are the v10 review's BINDING ADJUDICATIONS and are
# implemented here, not as the card's v10 fixture text reads:
#   * MEDIUM-2: fixture (e) is a disjoint clang-vs-apiVersion tie; AD-2's order puts
#     apiVersion BEFORE clang, so apiVersion wins -> branch 1.  The card's v10 (e)
#     ("clang wins -> branch 2") contradicts AD-2's own tie-break and (d)'s
#     first-match result, so no AD-2-conformant implementation could satisfy it.
#   * LOW-1: fixture (b)'s reverse-order control flips the branch to 1, because
#     under the reverse order the clang+archive candidate is labelled
#     rejected_artifact_pin by first match -> {rejected_artifact_pin: 2}.
CLANG_AND_ARCHIVE = ('clang_revision_string', 'native_sdk_package_archive')
ARCHIVE_ONLY = ('target_libc',)
CLANG_ONLY = ('clang_revision_string',)
API_VERSION_ONLY = ('api_version',)
API_VERSION_AND_CLANG = ('api_version', 'clang_revision_string')

AD2_FIXTURES = (
    {
        'case': '(a)',
        'candidates': ({'id': 'clang+archive', 'failed': CLANG_AND_ARCHIVE},),
        'counts': {'rejected_clang_revision': 1},
        'deltas': CLANG_AND_ARCHIVE,
        'branch': '2_clang_revision_mismatch',
    },
    {
        'case': '(b)',
        'candidates': (
            {'id': 'clang+archive', 'failed': CLANG_AND_ARCHIVE},
            {'id': 'archive-only', 'failed': ARCHIVE_ONLY},
        ),
        'counts': {'rejected_clang_revision': 1, 'rejected_artifact_pin': 1},
        'deltas': CLANG_AND_ARCHIVE + ARCHIVE_ONLY,
        'branch': '2_clang_revision_mismatch',
        'reverse_counts': {'rejected_artifact_pin': 2},
        'reverse_branch': '1_no_admissible_sdk',
    },
    {
        'case': '(c)',
        'candidates': ({'id': 'archive-only', 'failed': ARCHIVE_ONLY},),
        'counts': {'rejected_artifact_pin': 1},
        'deltas': ARCHIVE_ONLY,
        'branch': '1_no_admissible_sdk',
    },
    {
        'case': '(d)',
        'candidates': ({'id': 'apiVersion+clang', 'failed': API_VERSION_AND_CLANG},),
        'counts': {'rejected_api_version': 1},
        'deltas': API_VERSION_AND_CLANG,
        'branch': '1_no_admissible_sdk',
    },
    {
        'case': '(e)',
        'candidates': (
            {'id': 'clang-only', 'failed': CLANG_ONLY},
            {'id': 'apiVersion-only', 'failed': API_VERSION_ONLY},
        ),
        'counts': {'rejected_clang_revision': 1, 'rejected_api_version': 1},
        'deltas': CLANG_ONLY + API_VERSION_ONLY,
        'branch': '1_no_admissible_sdk',
        'reverse_counts': {'rejected_clang_revision': 1, 'rejected_api_version': 1},
        'reverse_branch': '2_clang_revision_mismatch',
    },
)
REVERSED_AD2_ORDER = tuple(reversed(AD2_DISPOSITION_ORDER))

# TEST-F106-04 "Admissibility lattice and stop semantics".  Each vector names the
# one dimension it varies (CODE-011: a negative that fails for a reason other than
# the one it names pins nothing).
AD2_LATTICE_VECTORS = (
    {'case': '(i)', 'failed': ARCHIVE_ONLY, 'disposition': 'rejected_artifact_pin',
     'adoption_mode': None, 'reason': 'the 6.1-Release candidate fails the libc.so pin'},
    {'case': '(ii)', 'failed': (), 'disposition': 'adopted_version_deviation',
     'adoption_mode': 'version_deviation', 'reason': 'all pins identical, declared version is the record adopted value'},
    {'case': '(iii)', 'failed': (), 'disposition': 'adopted_frozen_exact',
     'adoption_mode': 'frozen_exact', 'reason': 'all pins identical, declared version is the frozen value'},
    {'case': '(iv)', 'failed': CLANG_ONLY, 'disposition': 'rejected_clang_revision',
     'adoption_mode': None, 'reason': 'clang revision differs: reject this candidate and continue'},
    {'case': '(v)', 'failed': ('unrecorded_version',), 'disposition': 'rejected_unrecorded_version',
     'adoption_mode': None, 'reason': 'declared version is neither the frozen nor the adopted value'},
)
LATTICE_WINNER_PAIR = ('(ii)', '(iii)')


_MISSING = object()


def _leaf_items(value: Any, prefix: tuple[str, ...] = ()) -> list[tuple[tuple[str, ...], Any]]:
    leaves: list[tuple[tuple[str, ...], Any]] = []
    if isinstance(value, dict):
        for key, item in value.items():
            leaves.extend(_leaf_items(item, prefix + (str(key),)))
    elif isinstance(value, list):
        for index, item in enumerate(value):
            leaves.extend(_leaf_items(item, prefix + (str(index),)))
    else:
        leaves.append((prefix, value))
    return leaves


def _lookup(node: Any, path: tuple[str, ...]) -> Any:
    for key in path:
        if isinstance(node, dict):
            if key not in node:
                return _MISSING
            node = node[key]
        elif isinstance(node, list):
            if not key.isdigit() or int(key) >= len(node):
                return _MISSING
            node = node[int(key)]
        else:
            return _MISSING
    return node


def _decision_record_adopted_version(record: dict[str, Any]) -> str | None:
    """The decision record's adopted declared version: the linux_sdk key that is not the frozen one."""
    frozen = str((record.get('frozen_pins') or {}).get('sdk_version') or '')
    candidates = [str(version) for version in (record.get('linux_sdk') or {}) if str(version) != frozen]
    return candidates[0] if len(candidates) == 1 else None


_FIXTURE_KINDS = {
    'clang_revision_string': 'revision_string',
    'linux_clang_binary': 'file_sha256',
    'ohos_toolchain_cmake': 'file_sha256',
    'target_libc': 'file_sha256',
    'native_sdk_package_archive': 'archive_sha256',
    'toolchains_sdk_package_archive': 'archive_sha256',
}


def _fixture_observed(record: dict[str, Any], failed: Any) -> dict[str, Any]:
    """A synthetic examined candidate in the pinned §Data Structures shape.

    ``dimensions[]`` carries exactly one entry per declared pin (the H2 length
    assertion).  Entries named in ``failed`` are marked unequal and every other
    entry equal, so the candidate differs from a conformant one in exactly the
    dimensions it names (CODE-011 R1: a negative must isolate its rule).
    """
    frozen = str((record.get('frozen_pins') or {}).get('sdk_version') or '')
    declared = _decision_record_adopted_version(record) or frozen
    if 'unrecorded_version' in failed:
        declared = f'{frozen}-unrecorded'
    dimensions = [
        {
            'name': name,
            'kind': _FIXTURE_KINDS[name],
            'source_path': str(DECISION_RECORD),
            'source_key': f'linux_sdk.{declared}',
            'observed': f'fixture-observed-{name}',
            'expected': f'fixture-expected-{name}',
            'comparable': True,
            'reason': None,
            'equal': name not in failed,
        }
        for name in DECLARED_PINS
    ]
    observed: dict[str, Any] = {
        'declared_version': declared,
        'apiVersion': '22' if 'api_version' in failed else '23',
        'dimensions': dimensions,
    }
    if 'unavailable' in failed:
        observed['available'] = False
    return observed


def _shell_quote(text: str) -> str:
    return "'" + text.replace("'", "'\"'\"'") + "'"


def _digest_fields(value: Any, prefix: str = '') -> list[tuple[str, Any]]:
    found: list[tuple[str, Any]] = []
    if isinstance(value, dict):
        for key, item in value.items():
            label = f'{prefix}.{key}' if prefix else str(key)
            if key == 'sha256' or key.endswith('_sha256'):
                found.append((label, item))
            else:
                found.extend(_digest_fields(item, label))
    elif isinstance(value, list):
        for index, item in enumerate(value):
            found.extend(_digest_fields(item, f'{prefix}[{index}]'))
    return found


def _recorded_artifact_digests(lock: dict[str, Any]) -> list[tuple[str, str, str]]:
    """Artifacts whose Linux path and digest the lock records together."""
    entries: list[tuple[str, str, str]] = []
    sdk = lock.get('sdk', {})
    clang = sdk.get('clang', {})
    if clang.get('path') and clang.get('sha256'):
        entries.append(('sdk.clang', str(clang['path']), str(clang['sha256']).upper()))
    for name, record in (sdk.get('target_artifacts') or {}).items():
        if isinstance(record, dict) and record.get('path') and record.get('observed_sha256'):
            entries.append((f'sdk.target_artifacts.{name}', str(record['path']), str(record['observed_sha256']).upper()))
    for name, record in (sdk.get('packages') or {}).items():
        if isinstance(record, dict) and record.get('path') and record.get('sha256'):
            entries.append((f'sdk.packages.{name}', str(record['path']), str(record['sha256']).upper()))
    boot_jdk = lock.get('boot_jdk', {})
    if boot_jdk.get('path') and boot_jdk.get('sha256'):
        entries.append(('boot_jdk', str(boot_jdk['path']), str(boot_jdk['sha256']).upper()))
    return entries


if __name__ == '__main__':
    unittest.main()
