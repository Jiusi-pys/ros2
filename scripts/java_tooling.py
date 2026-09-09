#!/usr/bin/env python3
"""Reproduce the F089 Java host tooling from content-addressed inputs.

This runner never downloads dependencies.  It verifies the caller-provided
cache, extracts tools into the requested output directory, and builds only
copied source trees with Gradle's offline mode enabled.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import zipfile
from pathlib import Path, PurePosixPath
from typing import Any, Iterable


REPLAY_SCHEMA = "mdds.java-tooling-replay/v1"
CONSUMER_SCHEMA = "mdds.java-tooling-consumer/v1"
LOCK_SCHEMA = "mdds.java-host-tooling-lock/v1"
SOURCE_LOCK_SCHEMA = "mdds.java-source-input-lock/v1"
GRADLE_TASK = re.compile(r"^> Task (?P<task>:\S+)", re.MULTILINE)
APPROVAL_BUNDLE_SHA256 = (
    "D63735E9CDFB485D789358B34081F0193897BE378E35402E8921114703E30A82"
)
EXPECTED_SOURCES = {
    "ros2_java_seed": (
        "https://github.com/EGAlberts/ros2_java.git",
        "9f344d7657e1d2a81f872957eafdb1539f08eb9b",
        "src/Jiusi-pys/ros2_java",
    ),
    "ros2_java_upstream": (
        "https://github.com/ros2-java/ros2_java.git",
        "d0e4e952bad1977ce9818d00b520d3ad0aeafd8d",
        None,
    ),
    "ament_java": (
        "https://github.com/ros2-java/ament_java.git",
        "c430324f0e8aa12106e67799795994b8aede10a9",
        "src/ros2-java/ament_java",
    ),
    "ament_gradle_plugin": (
        "https://github.com/EGAlberts/ament_gradle_plugin.git",
        "f1addd56b7ae1d8182e0aac23759313d5f354894",
        "src/Jiusi-pys/ament_gradle_plugin",
    ),
    "ros2_java_examples": (
        "https://github.com/EGAlberts/ros2_java_examples.git",
        "7e965265c17c81ab28f3c93b84f2af8c60da5b7c",
        "src/Jiusi-pys/ros2_java_examples",
    ),
}
EXPECTED_TOOLS = {
    "jdk": (
        "jdk-21.0.12+8",
        "OpenJDK21U-jdk_x64_windows_hotspot_21.0.12_8.zip",
        "9ba963ee2371874a74185d18bc7bb2ab9407df7683300855ed7606e0662321d0",
    ),
    "gradle": (
        "8.5.0",
        "gradle-8.5-bin.zip",
        "9d926787066a081739e8200858338b4a69e837c3a821a33aca9db09dd4a41026",
    ),
}


class ToolingError(RuntimeError):
    """A fail-closed qualification or build error."""


def _read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ToolingError(f"cannot read JSON {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ToolingError(f"JSON root must be an object: {path}")
    return value


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _canonical_sha256(value: Any) -> str:
    encoded = json.dumps(
        value, ensure_ascii=True, separators=(",", ":"), sort_keys=True
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _within(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
    except ValueError:
        return False
    return True


def _workspace_file(workspace: Path, relative: str, description: str) -> Path:
    candidate = (workspace / relative).resolve()
    if not _within(candidate, workspace):
        raise ToolingError(f"{description} escapes workspace: {relative}")
    return candidate


def _prepare_output(output: Path, workspace: Path, clean: bool) -> None:
    resolved = output.resolve()
    if resolved == workspace or _within(workspace, resolved):
        raise ToolingError(f"unsafe output directory: {resolved}")
    source_root = (workspace / "src").resolve()
    if resolved == source_root or _within(resolved, source_root):
        raise ToolingError(f"output directory may not be inside source tree: {resolved}")
    if resolved.exists() and clean:
        shutil.rmtree(resolved)
    if resolved.exists() and any(resolved.iterdir()):
        raise ToolingError(f"output directory is not empty (use --clean): {resolved}")
    resolved.mkdir(parents=True, exist_ok=True)


def _run(
    command: list[str],
    *,
    cwd: Path,
    environment: dict[str, str] | None = None,
    timeout: int = 300,
) -> str:
    try:
        completed = subprocess.run(
            command,
            cwd=cwd,
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=timeout,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise ToolingError(f"command could not complete: {command[0]}: {exc}") from exc
    if completed.returncode != 0:
        raise ToolingError(
            f"command failed ({completed.returncode}): {' '.join(command)}\n"
            f"{completed.stdout}"
        )
    return completed.stdout


def _validate_lock(lock_path: Path, workspace: Path) -> dict[str, Any]:
    lock = _read_json(lock_path)
    if lock.get("schema") != LOCK_SCHEMA:
        raise ToolingError(f"unsupported lock schema in {lock_path}")
    if lock.get("approval_bundle_sha256") != APPROVAL_BUNDLE_SHA256:
        raise ToolingError("approval bundle identity does not match F089")
    sources = lock.get("sources")
    if not isinstance(sources, dict) or set(sources) != set(EXPECTED_SOURCES):
        raise ToolingError("source inventory does not match F089")
    for name, (url, commit, workspace_path) in EXPECTED_SOURCES.items():
        source = sources.get(name)
        if not isinstance(source, dict) or (
            source.get("url"), source.get("commit"), source.get("workspace_path")
        ) != (url, commit, workspace_path):
            raise ToolingError(f"source identity does not match F089: {name}")
    tools = lock.get("tools")
    if not isinstance(tools, dict):
        raise ToolingError("tool inventory does not match F089")
    for name, (version, filename, sha256) in EXPECTED_TOOLS.items():
        tool = tools.get(name)
        artifact = tool.get("artifact") if isinstance(tool, dict) else None
        if not isinstance(artifact, dict) or (
            tool.get("version"), artifact.get("filename"), artifact.get("sha256")
        ) != (version, filename, sha256):
            raise ToolingError(f"tool identity does not match F089: {name}")
    for item in lock.get("dependency_locks", []):
        if not isinstance(item, dict):
            raise ToolingError("dependency lock entry must be an object")
        relative = str(item.get("path", ""))
        expected = str(item.get("sha256", "")).lower()
        path = _workspace_file(workspace, relative, "dependency lock")
        if not path.is_file():
            raise ToolingError(f"missing dependency lock: {path}")
        actual = _sha256(path)
        if actual != expected:
            raise ToolingError(
                f"dependency lock checksum mismatch: {relative}: "
                f"expected {expected}, got {actual}"
            )
        _validate_source_lock(path, workspace)
    return lock


def _validate_source_lock(path: Path, workspace: Path) -> None:
    source_lock = _read_json(path)
    if source_lock.get("schema") != SOURCE_LOCK_SCHEMA:
        raise ToolingError(f"unsupported source input lock schema: {path}")
    repository = path.parent
    for item in source_lock.get("inputs", []):
        relative = str(item.get("path", ""))
        candidate = (repository / relative).resolve()
        if not _within(candidate, repository) or not candidate.is_file():
            raise ToolingError(f"missing or unsafe source input: {candidate}")
        expected = str(item.get("sha256", "")).lower()
        normalization = item.get("normalization")
        if normalization == "lf":
            actual = hashlib.sha256(
                candidate.read_bytes().replace(b"\r\n", b"\n")
            ).hexdigest()
        elif normalization is None:
            actual = _sha256(candidate)
        else:
            raise ToolingError(
                f"unsupported source input normalization: {normalization}"
            )
        if expected != actual:
            raise ToolingError(
                f"source input checksum mismatch: {candidate}: "
                f"expected {expected}, got {actual}"
            )


def _validate_repository(workspace: Path, source: dict[str, Any]) -> Path:
    relative = source.get("workspace_path")
    if not isinstance(relative, str):
        raise ToolingError("required source has no workspace path")
    repository = _workspace_file(workspace, relative, "source repository")
    if not (repository / ".git").exists():
        raise ToolingError(f"source is not a Git repository: {repository}")
    commit = str(source.get("commit", ""))
    _run(["git", "cat-file", "-e", f"{commit}^{{commit}}"], cwd=repository, timeout=30)
    _run(["git", "merge-base", "--is-ancestor", commit, "HEAD"], cwd=repository, timeout=30)
    return repository


def _artifact(lock: dict[str, Any], name: str, cache: Path) -> Path:
    try:
        metadata = lock["tools"][name]["artifact"]
        filename = str(metadata["filename"])
        expected = str(metadata["sha256"]).lower()
    except (KeyError, TypeError) as exc:
        raise ToolingError(f"missing {name} artifact metadata") from exc
    if Path(filename).name != filename:
        raise ToolingError(f"unsafe {name} artifact filename: {filename}")
    path = cache / filename
    if not path.is_file():
        url = metadata.get("url", "")
        raise ToolingError(
            f"approved cache artifact is missing: {path}\n"
            f"populate it from {url}\nexpected SHA-256: {expected}"
        )
    expected_size = metadata.get("size")
    if isinstance(expected_size, int) and path.stat().st_size != expected_size:
        raise ToolingError(
            f"{name} artifact size mismatch: expected {expected_size}, "
            f"got {path.stat().st_size}"
        )
    actual = _sha256(path)
    if actual != expected:
        raise ToolingError(
            f"{name} artifact checksum mismatch: expected {expected}, got {actual}"
        )
    return path


def _safe_extract_zip(archive: Path, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as stream:
        for member in stream.infolist():
            path = PurePosixPath(member.filename)
            if path.is_absolute() or ".." in path.parts:
                raise ToolingError(f"unsafe ZIP member in {archive}: {member.filename}")
            mode = (member.external_attr >> 16) & 0xFFFF
            if stat.S_ISLNK(mode):
                raise ToolingError(f"symbolic-link ZIP member is not allowed: {member.filename}")
        stream.extractall(destination)


def _single_tool(root: Path, relative: str) -> Path:
    matches = sorted(path for path in root.glob(f"*/{relative}") if path.is_file())
    if len(matches) != 1:
        raise ToolingError(f"expected one extracted {relative}, found {len(matches)}")
    return matches[0]


def _tool_environment(java: Path, gradle: Path, output: Path) -> dict[str, str]:
    environment = dict(os.environ)
    java_home = java.parent.parent
    gradle_home = gradle.parent.parent
    environment["JAVA_HOME"] = str(java_home)
    environment["GRADLE_HOME"] = str(gradle_home)
    environment["GRADLE_USER_HOME"] = str(output / "tool-state" / "gradle-home")
    environment["PATH"] = os.pathsep.join(
        [str(java.parent), str(gradle.parent), environment.get("PATH", "")]
    )
    environment.pop("CLASSPATH", None)
    environment["JAVA_TOOL_OPTIONS"] = "-Duser.language=en -Duser.country=US -Dfile.encoding=UTF-8"
    return environment


def _validate_tool_versions(
    lock: dict[str, Any], java: Path, gradle: Path, environment: dict[str, str]
) -> None:
    java_output = _run([str(java), "-version"], cwd=java.parent, environment=environment)
    expected_java = str(lock["tools"]["jdk"]["version"])
    java_version = expected_java.removeprefix("jdk-")
    feature, build = java_version.split("+", 1)
    if feature not in java_output or f"+{build}" not in java_output:
        raise ToolingError(
            f"JDK version mismatch: expected {expected_java}, got:\n{java_output}"
        )
    gradle_output = _run(
        [str(gradle), "--version"], cwd=gradle.parent, environment=environment
    )
    expected_gradle = str(lock["tools"]["gradle"]["version"])
    displayed = expected_gradle[:-2] if expected_gradle.endswith(".0") else expected_gradle
    if not re.search(rf"^Gradle {re.escape(displayed)}$", gradle_output, re.MULTILINE):
        raise ToolingError(
            f"Gradle version mismatch: expected {expected_gradle}, got:\n{gradle_output}"
        )


def _copy_repository(source: Path, destination: Path) -> None:
    def ignored(_directory: str, names: list[str]) -> set[str]:
        return {
            name
            for name in names
            if name in {".git", ".gradle", "build", "__pycache__"}
            or name.endswith(".pyc")
        }

    shutil.copytree(source, destination, ignore=ignored)


def _deterministic_zip(source: Path, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(candidate for candidate in source.rglob("*") if candidate.is_file()):
            relative = path.relative_to(source).as_posix()
            info = zipfile.ZipInfo(relative, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, path.read_bytes(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)


def _gradle_command(gradle: Path, tasks: Iterable[str]) -> list[str]:
    return [
        str(gradle),
        "--offline",
        "--no-daemon",
        "--console=plain",
        "--stacktrace",
        *tasks,
    ]


def _copy_unique_jar(search_root: Path, output: Path) -> None:
    jars = sorted(path for path in search_root.rglob("*.jar") if path.is_file())
    if len(jars) != 1:
        raise ToolingError(
            f"expected one build JAR below {search_root}, found {len(jars)}: {jars}"
        )
    output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(jars[0], output)


def _tree_sha256(files: list[Path], base: Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(files, key=lambda item: item.relative_to(base).as_posix()):
        relative = path.relative_to(base).as_posix().encode("utf-8")
        digest.update(relative)
        digest.update(b"\0")
        digest.update(bytes.fromhex(_sha256(path)))
    return digest.hexdigest()


def _build_replay(
    workspace: Path,
    lock: dict[str, Any],
    java: Path,
    gradle: Path,
    environment: dict[str, str],
    output: Path,
) -> tuple[str, list[str]]:
    sources = lock["sources"]
    ament_java = _validate_repository(workspace, sources["ament_java"])
    plugin = _validate_repository(workspace, sources["ament_gradle_plugin"])

    copied_ament = output / "work" / "ament_java"
    copied_plugin = output / "work" / "ament_gradle_plugin"
    _copy_repository(ament_java, copied_ament)
    _copy_repository(plugin, copied_plugin)

    artifacts = output / "artifacts"
    ament_archive = artifacts / "ament_java-source.zip"
    plugin_jar = artifacts / "ament-gradle-plugin.jar"
    _deterministic_zip(copied_ament, ament_archive)
    gradle_output = _run(
        _gradle_command(gradle, ["clean", "f089HostToolingJar"]),
        cwd=copied_plugin,
        environment=environment,
        timeout=300,
    )
    _copy_unique_jar(copied_plugin / "build" / "libs", plugin_jar)
    return _tree_sha256([ament_archive, plugin_jar], output), GRADLE_TASK.findall(gradle_output)


def _quote_gradle_path(path: Path) -> str:
    return path.resolve().as_posix().replace("'", "\\'")


def _build_consumer(
    workspace: Path,
    lock: dict[str, Any],
    java: Path,
    gradle: Path,
    environment: dict[str, str],
    output: Path,
) -> tuple[str, list[str]]:
    sources = lock["sources"]
    consumer_repo = _validate_repository(workspace, sources["ros2_java_seed"])
    plugin_repo = _validate_repository(workspace, sources["ament_gradle_plugin"])

    copied_plugin = output / "work" / "ament_gradle_plugin"
    _copy_repository(plugin_repo, copied_plugin)
    plugin_output = _run(
        _gradle_command(gradle, ["clean", "f089HostToolingJar"]),
        cwd=copied_plugin,
        environment=environment,
        timeout=300,
    )
    plugin_jar = output / "inputs" / "ament-gradle-plugin.jar"
    _copy_unique_jar(copied_plugin / "build" / "libs", plugin_jar)

    consumer = output / "work" / "ros2_java_consumer"
    source_root = _workspace_file(
        workspace, str(lock["replay"]["consumer"]), "consumer source root"
    )
    if not _within(source_root, consumer_repo):
        raise ToolingError(f"consumer source root is outside ros2_java: {source_root}")
    java_root = consumer / "src" / "main" / "java"
    for relative in lock["replay"]["consumer_sources"]:
        source = (source_root / str(relative)).resolve()
        if not _within(source, source_root) or not source.is_file():
            raise ToolingError(f"missing locked consumer source: {source}")
        destination = java_root / str(relative)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)

    consumer.mkdir(parents=True, exist_ok=True)
    (consumer / "settings.gradle").write_text(
        "rootProject.name = 'f089-ros2-java-consumer'\n", encoding="utf-8"
    )
    (consumer / "build.gradle").write_text(
        f"""import org.gradle.api.tasks.bundling.AbstractArchiveTask

buildscript {{
  dependencies {{
    classpath files('{_quote_gradle_path(plugin_jar)}')
  }}
}}

apply plugin: 'java'
apply plugin: 'org.ros2.tools.gradle'

group = 'org.ros2.rcljava'
version = 'f089'

java {{
  sourceCompatibility = JavaVersion.VERSION_21
  targetCompatibility = JavaVersion.VERSION_21
}}

ament.entryPoints.consoleScripts = []

def f089Sources = fileTree('src/main/java') {{ include '**/*.java' }}

tasks.register('f089ConsumerTooling') {{
  group = 'verification'
  dependsOn tasks.named('storeAmentPropertiesTask')
  doLast {{
    if (f089Sources.empty) {{
      throw new GradleException('locked ros2_java consumer sources are absent')
    }}
  }}
}}
""",
        encoding="utf-8",
        newline="\n",
    )
    install = output / "consumer-install"
    properties = [
        f"-Pament.source_space={consumer}",
        f"-Pament.build_space={output / 'consumer-build'}",
        f"-Pament.install_space={install}",
        "-Pament.dependencies=",
        "-Pament.package_manifest.name=rcljava_common_f089",
        "-Pament.gradle_recursive_dependencies=false",
        "-Pament.exec_dependency_paths_in_workspace=",
    ]
    consumer_output = _run(
        [*_gradle_command(gradle, ["clean", "f089ConsumerTooling"]), *properties],
        cwd=consumer,
        environment=environment,
        timeout=300,
    )
    classes = consumer / "build" / "classes" / "java" / "main"
    classes.mkdir(parents=True, exist_ok=True)
    java_sources = sorted(path for path in java_root.rglob("*.java") if path.is_file())
    _run(
        [
            str(java.parent / "javac.exe"),
            "-encoding",
            "UTF-8",
            "-source",
            "21",
            "-target",
            "21",
            "-d",
            str(classes),
            *(str(path) for path in java_sources),
        ],
        cwd=consumer,
        environment=environment,
        timeout=120,
    )
    if not classes.is_dir() or not any(classes.rglob("*.class")):
        raise ToolingError(f"consumer compile produced no class files: {classes}")
    consumer_jar = install / "share" / "rcljava_common_f089" / "java" / "rcljava_common_f089.jar"
    _deterministic_zip(classes, consumer_jar)
    consumer_jars = [consumer_jar]
    tasks = [*GRADLE_TASK.findall(plugin_output), *GRADLE_TASK.findall(consumer_output)]
    return _tree_sha256(consumer_jars, output), tasks


def _common_parser(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--workspace", required=True, type=Path)
    parser.add_argument("--lock", required=True, type=Path)
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--cache-dir", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--offline", action="store_true")
    parser.add_argument("--clean", action="store_true")
    parser.add_argument("--json", action="store_true")


def _arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="operation", required=True)
    replay = subparsers.add_parser("replay")
    _common_parser(replay)
    consumer = subparsers.add_parser("consumer")
    consumer.add_argument("--consumer", required=True, choices=["ros2_java_seed"])
    _common_parser(consumer)
    return parser.parse_args()


def main() -> int:
    arguments = _arguments()
    workspace = arguments.workspace.resolve()
    source_root = arguments.source_root.resolve()
    lock_path = arguments.lock.resolve()
    cache = arguments.cache_dir.resolve()
    output = arguments.output_dir.resolve()
    if not arguments.offline:
        raise ToolingError("F089 permits only explicit --offline execution")
    if source_root != (workspace / "src").resolve():
        raise ToolingError(
            f"source root must be the workspace src directory: {workspace / 'src'}"
        )
    if not _within(lock_path, workspace):
        raise ToolingError(f"lock must be inside workspace: {lock_path}")

    lock = _validate_lock(lock_path, workspace)
    _prepare_output(output, workspace, arguments.clean)
    jdk_archive = _artifact(lock, "jdk", cache)
    gradle_archive = _artifact(lock, "gradle", cache)
    tool_root = output / "tools"
    _safe_extract_zip(jdk_archive, tool_root / "jdk")
    _safe_extract_zip(gradle_archive, tool_root / "gradle")
    java = _single_tool(tool_root / "jdk", "bin/java.exe")
    gradle = _single_tool(tool_root / "gradle", "bin/gradle.bat")
    environment = _tool_environment(java, gradle, output)
    _validate_tool_versions(lock, java, gradle, environment)

    fingerprint = _canonical_sha256(
        {
            "lock_sha256": _sha256(lock_path),
            "jdk_sha256": _sha256(jdk_archive),
            "gradle_sha256": _sha256(gradle_archive),
            "dependency_locks": lock["dependency_locks"],
        }
    )
    if arguments.operation == "replay":
        artifact_hash, tasks = _build_replay(
            workspace, lock, java, gradle, environment, output
        )
        receipt = {
            "schema": REPLAY_SCHEMA,
            "result": "PASS",
            "offline": True,
            "clean": bool(arguments.clean),
            "network_accessed": False,
            "input_fingerprint_sha256": fingerprint,
            "artifact_tree_sha256": artifact_hash,
            "executed_tasks": tasks,
        }
    else:
        artifact_hash, tasks = _build_consumer(
            workspace, lock, java, gradle, environment, output
        )
        receipt = {
            "schema": CONSUMER_SCHEMA,
            "result": "PASS",
            "consumer": arguments.consumer,
            "jdk_version": lock["tools"]["jdk"]["version"],
            "gradle_version": lock["tools"]["gradle"]["version"],
            "offline": True,
            "clean": bool(arguments.clean),
            "network_accessed": False,
            "input_fingerprint_sha256": fingerprint,
            "artifact_tree_sha256": artifact_hash,
            "executed_tasks": tasks,
        }
    print(json.dumps(receipt, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ToolingError as exc:
        print(json.dumps({"result": "FAIL", "error": str(exc)}, sort_keys=True))
        raise SystemExit(2)
