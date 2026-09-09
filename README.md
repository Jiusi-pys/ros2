# ROS 2 for OpenHarmony (RK3588A)

This fork contains a ROS 2 Jazzy Jalisco port for OpenHarmony
(`aarch64-linux-ohos`, musl libc), developed on RK3588A / KaihongOS. The
workspace has historically cross-built 364 packages, but that is build
evidence for the recorded source snapshot—not a blanket runtime claim for
every package or RMW.

Current generic-port verification is deliberately reported by layer:

- Fast DDS is the default RMW candidate. Its focused RK3588A regression gate
  has passed; a newly built release must pass the generic acceptance suite too.
- CPython, C++/Python messaging, CLI, services, actions, rosbag2 replay, decoded
  tracing events and process cleanup are mandatory generic runtime gates.
- GUI is experimental; SHM is disabled by default and experimental. DDS
  Security/TLS and Connext are outside this release profile.

See [the support matrix](docs/kaihongos_support_matrix.md) for evidence limits.

### Fast DDS RK3588A evidence candidate

Fast-DDS commit `e66e90fd7b1e7a14d80129002474c68bc16da2bb`
(`fix(ohos): anchor topic RTTI and use matching bundled TinyXML2`) has been
cross-built for OpenHarmony with `rmw_fastrtps`
`54d2240637f18e3ec710bb73c51c79abaf91ece4`. The OHOS-specific change gives
`TopicDescription` one out-of-line RTTI key function in `libfastrtps`, avoiding
cross-DSO `dynamic_cast` failures in the libc++abi environment. The bundled
TinyXML2 header and implementation are selected as one source pair when the
third-party option is forced.

On two RK3588A boards, the base deployment prefix
`/data/local/tmp/ros2/lib` (not a temporary overlay) has passed the RTTI symbol
contract, the `TestSubscriptionUse.no_content_filter_set` regression, all 16
selected `rmw_fastrtps_cpp` executables, all 16 selected
`rmw_fastrtps_dynamic_cpp` executables, and true-default A-to-B ROS 2
communication with both `RMW_IMPLEMENTATION` and
`FASTDDS_BUILTIN_TRANSPORTS` unset. Both nodes selected `rmw_fastrtps_cpp` and
the subscriber received `Hello World: 1..8`.

This is targeted runtime evidence, not a blanket Fast DDS release claim. The
native Fast-DDS GTest target was not directly cross-run, dedicated SHM/zero-copy
coverage is experimental, and DDS Security/TLS is outside this profile. A
release must retain a separately signed FastDDS evidence manifest and archive
SHA-256 in an independent record or WORM store; a local hash seal alone is not
release authorization.

Source locking covers both the manifest's top-level repositories and build-time
vendor downloads. The OHOS `ament_vendor` hook resolves mutable upstream tags
through `cmake/ohos-vendor-sources.lock.json`, rejects unknown tag/URL pairs,
and checks archive SHA-256 before extraction. Foonathan's separate download
is pinned to a full public commit. Non-OHOS vendor behavior is unchanged.

This standalone OHOS workspace lives on the `stand` branch, based on `jazzy_ohos`.

## Quick start

Prerequisites: Windows host with Git Bash, [Pixi](https://pixi.sh/), the
OpenHarmony command-line-tools SDK (NDK), and `hdc` access to the board(s).

```bash
git clone -b stand git@github.com:Jiusi-pys/ros2.git
cd ros2
pixi install --locked

# Fetch the exact release bases into an empty retained directory, replay every
# patch/snapshot, initialize the exact Fast-DDS Asio/TinyXML2 gitlinks, and
# compare all resulting trees. Continue the release build in REPLAY_DIR.
REPLAY_DIR=/absolute/empty/ros2-ohos-replay
./scripts/verify_fresh_lock_replay.sh "$REPLAY_DIR"
cd "$REPLAY_DIR"
pixi install --locked

# Select one SDK consistently for dependency build, ROS build and provenance.
export OHOS_NATIVE_SDK=C:/absolute/path/to/native
export HDC=C:/absolute/path/to/hdc.exe

# Materialize hash-verified Python inputs at python_target/usr and
# python_target/sitepkgs, plus the runtime archive and its .manifest.json.
# Use the public-source CPython workflow below, not a runtime pulled from a board.
export OHOS_PYTHON_RUNTIME_ARCHIVE="$PWD/python_target/runtime-artifacts/cpython-3.12.7-ohos-aarch64-source.tar.gz"
pixi run python scripts/python_target.py verify-runtime --root python_target/usr
pixi run python scripts/python_target.py verify-stage --site python_target/sitepkgs

# Requires an absent install prefix and no downloaded/extracted dependency cache.
./target_deps_src/build_all_clean_ohos.sh
OHOS_REQUIRE_CLEAN=1 ./scripts/build_ohos.sh

# Use the COMPLETE receipt path printed by the build; never substitute a log.
export OHOS_BUILD_RECEIPT=/absolute/path/to/ohos_build_receipt.json
./scripts/deploy_python_runtime_artifact.sh "$OHOS_PYTHON_RUNTIME_ARCHIVE" BOARD_A BOARD_B
# Select the isolated Python prefix printed by runtime deployment.
python_archive_sha=$(sha256sum "$OHOS_PYTHON_RUNTIME_ARCHIVE" | cut -d ' ' -f1)
export PYTHON_REMOTE_PREFIX="/data/python312-rk3588a-verify-${python_archive_sha:0:12}"
PYTHON_REQUIRE_RUNTIME_ARTIFACT=1 \
  PYTHON_RUNTIME_ARTIFACT_ARCHIVE="$OHOS_PYTHON_RUNTIME_ARCHIVE" \
  PYTHON_RUNTIME_ARTIFACT_MANIFEST="$OHOS_PYTHON_RUNTIME_ARCHIVE.manifest.json" \
  ./scripts/install_board_python_deps.sh BOARD_A BOARD_B
./scripts/deploy_ohos_generic.sh BOARD_A BOARD_B
ROS2_BOARD_A=BOARD_A ROS2_BOARD_B=BOARD_B ./scripts/run_ohos_generic_acceptance.sh
```

The locked interface, full runtime artifact, source-build receipt and Python
overlay must first be materialized and verified. Do not obtain an arbitrary
board runtime and call it publicly source-reproducible. Generic deployment binds the COMPLETE build
receipt, archive, exact source state, SDK and Python payloads. Acceptance checks
both boards' OS identities and payload trees before and after runtime tests.
The clean ROS receipt and generic release collector reject artifact-only Python
inputs; the legacy artifact deployment path remains available for diagnostics.
Generic deployment uses `/data/local/tmp/ros2-generic`, leaving the legacy
`/data/local/tmp/ros2` tree untouched. Here, clean-board acceptance means an
isolated payload/environment with no pre-existing acceptance processes; it
does not mean reflashing the OS or deleting unrelated board data.

### Public-source CPython input

The source recipe has completed an empty-directory build of CPython 3.12.7
with 70 target extension modules. This is source-build evidence, not a substitute
for the Python and ROS board-runtime gates. The
[input lock](scripts/python_source/source_build.lock.json) pins the official
CPython archive, the `Jiusi-pys/python` port configuration at an exact commit,
Linux OHOS compiler/SDK inputs and the five native dependency sources. The
[entry point](scripts/python_source/rebuild_source_release.sh) verifies the
prepared source tree and the recipe hashes before and after the build.

Run that entry point in a Linux build environment (the verified host is WSL
Ubuntu 20.04 with GNU build tools, Git LFS and curl), from this checkout:

```bash
bash scripts/python_source/rebuild_source_release.sh \
  --output /var/tmp/cpython-ohos-release
```

The output path must not exist. An optional `--cache` accepts only downloaded
archives that match the public input lock; it never reuses a build or extracted
source tree. Retain `source-build.trace`, the runtime archive and
`release/PYTHON_SOURCE_BUILD_RECEIPT.json`. Copy the two release files, without
renaming them, into the Windows workspace's `python_target/runtime-artifacts/`.
Then, in Git Bash:

```bash
export OHOS_PYTHON_RUNTIME_ARCHIVE="$PWD/python_target/runtime-artifacts/cpython-3.12.7-ohos-aarch64-source.tar.gz"
pixi run python scripts/python_runtime_artifact.py seal \
  --archive "$OHOS_PYTHON_RUNTIME_ARCHIVE" --origin public-source-build \
  --source-build-receipt "$PWD/python_target/runtime-artifacts/PYTHON_SOURCE_BUILD_RECEIPT.json"
./scripts/pull_python_target.sh --runtime-usr /path/to/source-build/target-build/runtime/usr
pixi run python scripts/python_target.py fetch-artifacts
./scripts/stage_python_wheels.sh
```

The `usr` path must expose the just-built runtime to Git Bash; no existing board
runtime is used. The seal and staging commands reject outputs that do not match
the checked-in Python lock. That lock identifies one accepted candidate: a new
build receipt can require a reviewed output-lock update even when its source
inputs are unchanged. A different source/SDK/recipe likewise needs a new reviewed
lock and build receipt, not a bypass of these checks. The receipt
binds the actual build and output hashes; cross-host bit-for-bit reproducibility
and cryptographic attestation are not claimed.

For source-runtime overlay acceptance, use
`scripts/verify_python_source_overlay.py`. The older
`scripts/python_source/board_overlay_probe.py` is retained only as a frozen
recipe companion for reproducing the recorded build-input digest; it is not
an acceptance entry point. Its historical prefix assertion rejects even the
current isolated deployment. The replacement rejects the legacy runtime and
other verification prefixes while allowing only the expected current prefix.

## Board environment for the compatibility deployment

`deploy_ohos.sh` stages the compatibility prefix at `/data/local/tmp/ros2`.
The generic release workflow above uses `deploy_ohos_generic.sh` and its own
isolated prefix.

On the board, run commands only when the deployed environment was accepted:

```sh
if . /data/local/tmp/ros2/env.sh; then
  ros2 --help                       # deployment-owned wrapper, not /usr/local/bin/ros2
  "$ROS2_TALKER_RAW"
fi
```

Automated callers must use `. /data/local/tmp/ros2/env.sh || exit 70` (or an
equivalent checked conditional). A failed source can return to its caller;
running the next command unconditionally could reuse an inherited stale
overlay.

See [AGENTS.md](AGENTS.md) for the full porting details (toolchain, musl
quirks, Qt/OGRE recipes, board-test infrastructure, debugging tips).

## Reproducible port snapshot

`ros2.repos` remains the moving development manifest. A release uses
`ros2.ohos.lock.repos`, whose revisions are immutable commit IDs. OHOS changes
are represented in `patches/` as:

- `.patch`, `.base`, `.tree`: unpublished commit series and expected tree;
- `.snapshot.patch`, `.snapshot.base`, `.snapshot.tree`: tracked plus
  non-ignored untracked working-tree state captured through a temporary Git
  index. The real subrepo index/worktree is not changed.

- `./scripts/export_patches.sh` — export all unpublished commits and current
  worktree snapshots from the repositories listed in the manifest.
- `pixi run python scripts/freeze_ros2_repos.py` — regenerate the immutable
  base manifest after exporting patches.
- `./scripts/apply_patches.sh` — replay `patches/` onto a fresh
  locked-manifest checkout and verify the resulting commit/worktree trees.
- `./scripts/verify_fresh_lock_replay.sh /path/to/empty-dir` — perform a real
  all-repository locked import, apply every series/snapshot, and compare all
  reconstructed worktree trees with the source workspace. The evidence
  directory is intentionally retained.

Deliberately refreshing against upstream ROS 2 (development, not release
replay):

```bash
vcs pull src/                  # fetch upstream updates/rebase each port branch
./scripts/export_patches.sh
pixi run python scripts/freeze_ros2_repos.py
```

Before release, reproduce in an empty directory using the lock manifest and
run the full build and relevant board/E2E gates. An unpushed owned-repository
HEAD is acceptable only while its complete fallback series/snapshot is
present; pushing remains a human provenance gate and is never done by these
scripts.

# About 
The Robot Operating System (ROS) is a set of software libraries and tools that help you build robot applications.
From drivers to state-of-the-art algorithms, and with powerful developer tools, ROS has what you need for your next robotics project.
And it's all open source.
Full project details on [ROS.org](https://ros.org/)

# Getting Started 
Looking to get started with ROS?
Our [installation guide is here](https://www.ros.org/blog/getting-started/).
Once you've installed ROS start by learning some [basic concepts](https://docs.ros.org/en/rolling/Concepts/Basic.html) and take a look at our [beginner tutorials](https://docs.ros.org/en/rolling/Tutorials/Beginner-CLI-Tools.html).

# Join the ROS Community

## Community Resources

* [ROS Discussion Forum](https://discourse.ros.org/)
* [ROS Zulip Server](https://openrobotics.zulipchat.com/)
* [Robotics Stack Exchange](https://robotics.stackexchange.com/) (preferred ROS support forum).
* [Official ROS Videos](https://vimeo.com/osrfoundation)
* [ROSCon](https://roscon.ros.org), our yearly developer conference. 
* Cite ROS 2 in academic work using [DOI: 10.1126/scirobotics.abm6074](https://www.science.org/doi/10.1126/scirobotics.abm6074) 

## Developer Resources
* [ROS 2 Documentation](https://docs.ros.org/)
* [ROS Package API reference](https://docs.ros.org/en/rolling/p/)
* [ROS Package Index](https://index.ros.org/)
* [ROS on Docker Hub](https://hub.docker.com/_/ros/)
* [ROS Resource Status Page](https://status.openrobotics.org/)
* [REP-2000](https://ros.org/reps/rep-2000.html): ROS 2 Releases and Target Platforms

## Project Resources
* [Purchase ROS Swag](https://spring.ros.org/)
* [Information about the ROS Trademark](https://www.ros.org/blog/media/)
* On Social Media
  * [Open Robotics on LinkedIn](https://www.linkedin.com/company/open-source-robotics-foundation)
  * [Open Robotics on Twitter](https://twitter.com/OpenRoboticsOrg)
  * [ROS.org on Twitter](https://twitter.com/ROSOrg)

ROS is made possible through the generous support of open source contributors and the non-profit [Open Source Robotics Foundation (OSRF)](https://www.openrobotics.org/).
Tax deductible donations to the OSRF can be [made here.](https://donorbox.org/support-open-robotics?utm_medium=qrcode&utm_source=qrcode)
