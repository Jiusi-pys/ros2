#!/usr/bin/env python3
"""Fail-closed static validation for KaihongOS release inputs."""

from __future__ import annotations

import re
import json
import sys
from pathlib import Path

import yaml


ROOT = Path(__file__).resolve().parents[1]
HEX40 = re.compile(r"^[0-9a-f]{40}$")
HEX64 = re.compile(r"^[0-9a-f]{64}$")


def fail(message: str) -> None:
    raise ValueError(message)


def load_repositories(path: Path) -> dict[str, dict[str, str]]:
    data = yaml.safe_load(path.read_text(encoding="utf-8"))
    repositories = data.get("repositories") if isinstance(data, dict) else None
    if not isinstance(repositories, dict) or not repositories:
        fail(f"{path.name}: missing repositories mapping")
    for name, spec in repositories.items():
        if not isinstance(spec, dict) or set(spec) != {"type", "url", "version"}:
            fail(f"{path.name}: malformed repository entry: {name}")
    return repositories


def verify_manifests() -> int:
    source = load_repositories(ROOT / "ros2.repos")
    locked = load_repositories(ROOT / "ros2.ohos.lock.repos")
    if source.keys() != locked.keys():
        fail("manifest and lock repository order/key set differ")
    for name, source_spec in source.items():
        lock_spec = locked[name]
        if source_spec["type"] != lock_spec["type"] or source_spec["url"] != lock_spec["url"]:
            fail(f"lock changes type/url for {name}")
        if not HEX40.fullmatch(str(lock_spec["version"])):
            fail(f"lock revision is not an immutable commit SHA: {name}")
    return len(locked)


def verify_patch_inventory() -> int:
    patch_dir = ROOT / "patches"
    count = 0
    for patch in sorted(patch_dir.glob("*.patch")):
        if patch.name.endswith(".snapshot.patch"):
            stem = patch.name.removesuffix(".snapshot.patch")
            metadata = [
                patch_dir / f"{stem}.snapshot.base",
                patch_dir / f"{stem}.snapshot.tree",
            ]
        else:
            stem = patch.stem
            metadata = [patch_dir / f"{stem}.base", patch_dir / f"{stem}.tree"]
        if patch.stat().st_size == 0:
            fail(f"empty patch: {patch.relative_to(ROOT)}")
        for item in metadata:
            if not item.is_file() or not HEX40.fullmatch(item.read_text(encoding="ascii").strip()):
                fail(f"missing or malformed patch metadata: {item.relative_to(ROOT)}")
        count += 1
    return count


def parse_source_lock() -> dict[str, tuple[str, str, str]]:
    path = ROOT / "target_deps_src" / "sources.lock"
    records: dict[str, tuple[str, str, str]] = {}
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        fields = line.split()
        if len(fields) != 4:
            fail(f"sources.lock:{number}: expected four fields")
        kind, name, digest, url = fields
        if kind not in {"archive", "git"}:
            fail(f"sources.lock:{number}: unknown kind {kind}")
        if name in records:
            fail(f"sources.lock:{number}: duplicate source {name}")
        if not url.startswith("https://"):
            fail(f"sources.lock:{number}: URL is not HTTPS")
        if kind == "archive" and not HEX64.fullmatch(digest):
            fail(f"sources.lock:{number}: malformed SHA-256")
        if kind == "git" and not HEX40.fullmatch(digest):
            fail(f"sources.lock:{number}: malformed commit SHA")
        records[name] = (kind, digest, url)
    if not records:
        fail("sources.lock contains no records")
    return records


def verify_recipe_references(records: dict[str, tuple[str, str, str]]) -> None:
    paths = [ROOT / "scripts" / "build_target_deps.sh"]
    paths.extend(sorted((ROOT / "target_deps_src").glob("*.sh")))
    paths.extend(sorted((ROOT / "target_deps_src" / "pyqt").glob("*.sh")))
    paths.extend(sorted((ROOT / "target_deps_src" / "qt_smoke").glob("*.sh")))
    paths.extend(
        ROOT / "target_deps_src" / "ohos-autotools-bin" / name
        for name in ("ohos-cc", "ohos-cxx")
    )
    paths.append(ROOT / "target_deps_src" / "qt-host-tools" / "cross-qmake.bat")
    referenced: set[str] = set()
    pattern = re.compile(r"(?:fetch_locked|ensure_locked_git_checkout)\s+([A-Za-z0-9_.+-]+)")
    forbidden = re.compile(r"C:[/\\]Users[/\\]")
    for path in paths:
        text = path.read_text(encoding="utf-8")
        if forbidden.search(text):
            fail(f"workstation-specific path remains in {path.relative_to(ROOT)}")
        if "curl " in text and path.name != "locked_sources.sh":
            fail(f"unlocked curl remains in {path.relative_to(ROOT)}")
        referenced.update(pattern.findall(text))
    missing = referenced - records.keys()
    if missing:
        fail(f"recipes reference unlocked sources: {sorted(missing)}")
    unused = records.keys() - referenced
    if unused:
        fail(f"sources.lock contains unreferenced inputs: {sorted(unused)}")


def verify_support_matrix() -> None:
    path = ROOT / "docs" / "kaihongos_support_matrix.md"
    rows: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.startswith("|"):
            continue
        columns = [column.strip().strip("`") for column in line.strip("|").split("|")]
        if len(columns) == 4:
            rows[columns[0]] = columns[1]
    expected = {
        "Fast DDS UDPv4": "Pending support gate",
        "Default RMW": "Pending support gate",
        "Qt5/PyQt5/rqt/turtlesim, headless": "Experimental",
        "Qt/RViz visible GUI": "Out of scope",
        "DDS shared memory (SHM)/iceoryx": "Experimental",
        "DDS Security, TLS, DTLS and SROS2": "Out of scope",
    }
    for capability, status in expected.items():
        if rows.get(capability) != status:
            fail(f"support matrix must classify {capability!r} as {status!r}")


def verify_vendor_lock() -> int:
    path = ROOT / "cmake" / "ohos-vendor-sources.lock.json"
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("schema_version") != 1 or not isinstance(data.get("sources"), list):
        fail("invalid transitive vendor source lock")
    identities = set()
    for entry in data["sources"]:
        kind = entry.get("type")
        digest_key = "commit" if kind == "git" else "sha256"
        if kind not in {"git", "zip", "tar"} or set(entry) != {
            "type", "url", "requested_version", digest_key
        }:
            fail("invalid transitive vendor source entry")
        identity = (kind, entry["url"], entry["requested_version"])
        if identity in identities or not entry["url"].startswith("https://"):
            fail("duplicate or non-public transitive vendor source")
        identities.add(identity)
        pattern = HEX40 if kind == "git" else HEX64
        if not pattern.fullmatch(entry[digest_key]):
            fail(f"invalid transitive vendor digest: {entry['url']}")
    if not identities:
        fail("empty transitive vendor lock")
    return len(identities)


def main() -> int:
    repositories = verify_manifests()
    patches = verify_patch_inventory()
    records = parse_source_lock()
    verify_recipe_references(records)
    verify_support_matrix()
    vendors = verify_vendor_lock()
    for required in (
        "target_deps_src/lttng-ust-2.13.8-ohos.patch",
        "target_deps_src/qtbase-5.15.8-ohos.patch",
        "target_deps_src/build_qtbase_ohos.sh",
        "target_deps_src/bootstrap_qt_host_tools.sh",
        "docs/kaihongos_support_matrix.md",
    ):
        if not (ROOT / required).is_file():
            fail(f"required release input missing: {required}")
    print(
        "RELEASE_INPUTS_VERIFIED "
        f"repositories={repositories} patches={patches} target_sources={len(records)} "
        f"transitive_vendor_sources={vendors}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, yaml.YAMLError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
