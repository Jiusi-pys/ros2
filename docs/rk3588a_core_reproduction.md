# RK3588A core port: reuse on another machine

This guide covers rcl, rclcpp, rclpy, runtime RMW selection, Fast DDS and
Cyclone DDS on KaihongOS. MDDS, rmw_mdds and mdds_gateway are not build or
runtime prerequisites. GUI and SHM are experimental; DDS Security/TLS is out
of scope. See the [support matrix](kaihongos_support_matrix.md) and
[2026-09-08 acceptance record](rk3588a_core_delivery_20260908.md).

## 1. Transfer the right inputs

Two different operations must not be confused:

- **Rebuild:** transfer the committed meta repository, then import
  `ros2.ohos.lock.repos` and replay `patches/`. Imported `src/` is ignored by
  the meta repository; the checked-in series and snapshots are the portable
  representation of its OHOS modifications, including the Python suffix fixes.
- **Reuse the accepted binary:** separately transfer the local `release/`
  bundle described in the delivery record, the CPython runtime archive plus
  source receipt/manifest, and the locked Python overlay inputs. The ROS
  archive alone is insufficient. These large artifacts are not stored in Git.
  Verify `SHA256SUMS` before using the bundle. Its `core-meta.patch` reconstructs
  the historical candidate against the recorded public base, not today's HEAD.

For an unpublished local commit, a normal GitHub clone does not contain it.
Transfer a Git bundle from the source machine, or publish the reviewed commit
through an independently authorized push. For example, from this meta repo:

```bash
git bundle create ../ros2-core-source.bundle jazzy_ohos
git bundle verify ../ros2-core-source.bundle
# On the receiving machine, into an absent destination:
git clone -b jazzy_ohos /path/to/ros2-core-source.bundle ros2-source
cd ros2-source
git rev-parse HEAD
```

Record that full commit ID in the new release record. Do not export all dirty
development work with `export_patches.sh` just to transfer a release: it also
captures unrelated imported repositories. Work from a clean committed clone.

## 2. Host prerequisites and fixed sources

The verified ROS build host is Windows with Git Bash and the checked-in Pixi
lock. Use short, space-free build paths on a case-insensitive filesystem.
CPython's separate source build uses Linux (verified: WSL Ubuntu 20.04), GNU
build tools, Git LFS and curl. This is not a claim that arbitrary Linux/macOS
hosts can run the Windows ROS build unchanged.

Install OpenHarmony SDK **23**, native 6.1.0.32 / Clang 15.0.4, and HDC.
Select your own SDK paths and board serials; never copy the former developer's
Windows username. In Git Bash:

```bash
export OHOS_NATIVE_SDK=C:/OpenHarmony/Sdk/23/native
export HDC=C:/OpenHarmony/Sdk/23/toolchains/hdc.exe
export ROS2_BOARD_A=YOUR_FIRST_HDC_SERIAL
export ROS2_BOARD_B=YOUR_SECOND_HDC_SERIAL
"$HDC" list targets
pixi install --locked
pixi run vcs import --input ros2.ohos.lock.repos src/
bash scripts/apply_patches.sh
```

Use the immutable lock, not `vcs pull` or the moving `ros2.repos`, for release
reproduction. Public source bases plus patch result trees are authoritative;
`.snapshot.base` and `.snapshot.tree` contain **tree** IDs, not commit IDs.
The manifest also contains independent MDDS repositories, but the core build
below excludes their packages. Importing them does not validate or enable them.

Follow [Public-source CPython input](../README.md#public-source-cpython-input)
in full: build into an absent Linux output directory, retain the source receipt,
seal the runtime archive, materialize `python_target/usr`, fetch locked overlay
inputs and stage wheels. The recipe pins official CPython 3.12.7 and the
configuration from `Jiusi-pys/python` to fixed inputs. Do not pull an arbitrary
installed board Python and label it source-reproduced.

An output-lock mismatch on a new host is a review gate, not permission to disable
verification. Source reproducibility does not imply byte-identical output.
If output hashes change, review/update the candidate locks and rerun the build
and board acceptance; the historical PASS belongs only to its recorded hashes.

## 3. Clean build and isolated dual-board deployment

From the imported source workspace, run
`bash scripts/verify_fresh_lock_replay.sh /c/ros2-core-clean` with an empty
destination. It imports/replays and compares source trees, retaining evidence.
Continue in that destination, run `pixi install --locked`, and materialize the
verified Python inputs there. Follow the [quick start](../README.md#quick-start)
for the exact runtime seal/staging and deployment variables.

Build dependencies using `bash target_deps_src/build_all_clean_ohos.sh`, then
`OHOS_BUILD_MDDS=OFF OHOS_REQUIRE_CLEAN=1 bash scripts/build_ohos.sh`.
Require a COMPLETE build receipt, not just a successful last package. Retain
the dependency receipt, vendor locks, SDK fingerprint, source snapshot, install
manifest and archive hash. Do not modify the sealed build workspace afterwards.

Deploy the verified CPython artifact and overlay to both explicit serials before
`bash scripts/deploy_ohos_generic.sh "$ROS2_BOARD_A" "$ROS2_BOARD_B"`.
Set `OHOS_BUILD_RECEIPT`, `OHOS_PYTHON_RUNTIME_ARCHIVE` and
`PYTHON_REMOTE_PREFIX` as shown in the quick start. The controller deploys
`/data/local/tmp/ros2-generic`, installs the hash-addressed ROS Python bootstrap,
and binds it in deployment provenance. It does not replace shared Python or
`/data/local/tmp/ros2`. Deployment success is not board acceptance.

## 4. Global CLI and offline doctor

The generic deployment controller does **not** install the global launcher or
doctor supplements. Provision these once per new board before handing it over;
afterwards users need no manual ROS environment exports. The launcher defaults
to Fast DDS, permits explicit Cyclone, and rejects MDDS selection.

Prepare the doctor payload on the host from the committed hash-locked wheels
(not `pip install` into the shared board Python). In Git Bash, from the repo:

```bash
doctor_work=$(mktemp -d)
mkdir "$doctor_work/wheels" "$doctor_work/site"
pixi run python -m pip download --no-deps --only-binary=:all: --require-hashes \
  -r scripts/runtime_config/doctor-requirements.txt -d "$doctor_work/wheels"
for wheel in "$doctor_work"/wheels/*.whl; do
  pixi run python -m zipfile -e "$wheel" "$doctor_work/site"
done
(cd "$doctor_work/site" && find . -type f -print0 | LC_ALL=C sort -z | \
  xargs -0 sha256sum -b | sed 's/ \*/  /') > "$doctor_work/doctor_manifest.sha256"
sha256sum "$doctor_work/doctor_manifest.sha256"
```

The manifest must equal
`0ff9eadde0e55db78bcbc08da1bf132b5dffc6393952344b73513d714fff3a07`,
the value embedded in `scripts/ros2_generic_global_launcher.sh`. Do not ignore
a mismatch or merely substitute a new hash in the launcher. Avoid bytecode,
pip-generated metadata and CRLF changes. Once verified, copy the manifest into
`site/doctor_manifest.sha256`, archive that directory's contents and stage them
on each board at:

`/data/local/tmp/ros2-core-config/doctor-python-0ff9eadde0e55db78bcbc08da1bf132b5dffc6393952344b73513d714fff3a07`

Verify the transferred archive hash and run `sha256sum -c doctor_manifest.sha256`
in that exact board directory. An archive recreated from identical files can
have a different compressed hash; record its actual hash separately. The
historical archive is supplied in the local release bundle if transferred.

For offline doctor metadata, fetch these two files from the fixed
`ros/rosdistro` commit `2b0767951219199c62c8fb28a1a225a301a162f4`, using
`https://raw.githubusercontent.com/ros/rosdistro/<commit>/<path>`:

| Path | SHA-256 |
| --- | --- |
| `index-v4.yaml` | `d515c14da215282a0b6ed456e6f6267633bbc8f6cf6abe379c0f2a25aeb6453f` |
| `jazzy/distribution.yaml` | `56a7b59a8e845e8ee50180194977780294111426a6251586c2622e9527e3222c` |

Verify both hashes before and after transfer, preserving the relative paths
under `/data/local/tmp/ros2-core-config/rosdistro/`. This is an offline Jazzy
snapshot, not a latest-version check. The global entry selects it automatically
when `ROSDISTRO_INDEX_URL` is unset; its hashes must be retained with deployment
evidence (the launcher does not itself validate these two metadata hashes).

Finally install the tracked `scripts/ros2_generic_global_launcher.sh` as
`/usr/local/bin/ros2` with mode 0755. First inspect and back up that **exact**
existing entry (including symlink target/type), and record its hash. Stage the
new file separately, verify its hash, then replace only that entry. If the OS
requires a temporary writable root mount, restore its original read-only state
after installation and on failure. Use your board's supported maintenance
procedure; do not reuse the old serial-specific migration scripts blindly.
Do not delete or move other deployment prefixes to make this work.

When using HDC in Git Bash, scope `MSYS2_ARG_CONV_EXCL='*'` to HDC calls only;
convert local file-send paths with `cygpath -w`. Do not export this variable
globally during dependency builds. HDC exit status can hide a remote failure:
require a remote success marker and the expected hash/check output.

## 5. Acceptance, provenance and recovery

Run the generic acceptance suite twice from the matching build/controller
context, with both serial variables set: once with
`ROS2_ACCEPTANCE_RMW=rmw_fastrtps_cpp`, then with
`ROS2_ACCEPTANCE_RMW=rmw_cyclonedds_cpp`. Require both result records to pass.
These cover CPython, C++/Python communication, CLI, default RMW, service/action,
rosbag2, decoded tracing and scoped process cleanup. Do not replace them with
`ros2 doctor` or a single talker startup.

Additionally, in a fresh shell on **each** board, with no manually sourced ROS
environment, verify:

```sh
ros2 --help
ros2 doctor
ros2 pkg prefix rclcpp
ros2 pkg prefix rclpy
RMW_IMPLEMENTATION=rmw_cyclonedds_cpp ros2 doctor --report
```

Require all five doctor checks, core prefixes under `ros2-generic`, and actual
Cyclone middleware identification in the explicit report. Record real network
interfaces, not just loopback. Preserve board serial/OS, RMW, source SHA/tree,
SDK, CPython and ROS package hashes, build receipt, bootstrap, launcher, doctor
payload/metadata hashes and both acceptance evidence manifests together.
Historical evidence did not include reboot testing; test a reboot separately
before claiming persistence across reboot on another OS image.

Retain the previous global entry and prefix until the new candidate passes.
On failure, restore only the entry/prefix saved for this deployment; never
delete shared Python, system DSoftBus or unrelated ROS/MDDS processes. The
2026-09-08 backup directory and retirement lists are evidence for the original
two boards, not universal cleanup instructions.
