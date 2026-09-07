# Doctor environment and Python dependencies

The deployed board environment lacked ROS_DISTRO, and importing doctor's
platform/package checks failed because rosdistro was absent. Doctor catches
entry-point import exceptions, so a reduced check count must not be accepted
as a complete diagnostic pass.

The Jazzy deployment template now exports ROS_DISTRO=jazzy, ROS_VERSION=2 and
ROS_PYTHON_VERSION=3 after its deployment/token guards. Two real source tests
were RED for both absent and inherited-wrong identities, then GREEN. The
existing fail-closed environment-source contract still passes.

The target Python lock now includes the missing pure-Python dependency closure:

| Package | Version | Wheel SHA-256 |
| --- | --- | --- |
| rosdistro | 1.1.0 | a10d0852fcef15b2b9c92a7b4d553481f660e4e672eaeb1138ad077ffe9a2a38 |
| rospkg | 1.6.2 | d8c32b8560501a9ca6616a1ef5f61ca89da79701f4a66020ada189594fe15f5a |
| distro | 1.9.0 | 7bffd925d65168f85027d8da9af6bddab658135b840670a223589bc0c8ef02b2 |

Versions and artifacts were obtained from official PyPI metadata and verified
before extraction. The target stage's existing native files were retained;
the stage was finalized and verified against the updated lock. Required-path
checks and the deployment import probe now include these three modules.

- Python lock SHA-256:
  `db28f3f48a1980016d27d2dc76e94b839cdd314322b1889fe9ef7d2fb0da927b`.
- Verified stage: 101 managed entries; tree SHA-256
  `510b017d2557325600ed514a749d384a363e4fcf5aff2d33b4e55761d0f8e96f`.
- Two dependency tests changed from RED to GREEN. The existing Python target
  fail-closed contract tests pass.
- Both RK3588A boards imported the exact three locked versions from private
  extracted wheels. Shell exit status, result and log hashes were verified.
  Evidence is in `../verification_evidence/goal1_20260906/doctor_dependencies/`.

This is dependency/environment evidence, not doctor or hello CLI acceptance.
The shared board Python installation has not been replaced. Changing the lock
invalidates older lock-bound runtime/deployment artifacts; those bindings must
be regenerated through the release workflow before final deployment. Do not
relabel an old artifact as belonging to the new lock.

## Full doctor and wtf acceptance

```bash
MDDS_RUN_ID=<fresh_id> MDDS_ROS_PROFILE_MODE=implicit MDDS_ROS_CLI_BATCH=diagnostics \
  bash scripts/run_mdds_broker_ros.sh
```

The diagnostic mode extracts the locked dependency wheels into the private
runtime, derives Jazzy environment assignments from the deployment template,
and supplies unmodified official rosdistro files from commit
`888f5d3a3f8a33fa8a7cf8deb86bdf8bccc3a1c1`. Both
[index-v4.yaml](https://raw.githubusercontent.com/ros/rosdistro/888f5d3a3f8a33fa8a7cf8deb86bdf8bccc3a1c1/index-v4.yaml)
and [Jazzy distribution.yaml](https://raw.githubusercontent.com/ros/rosdistro/888f5d3a3f8a33fa8a7cf8deb86bdf8bccc3a1c1/jazzy/distribution.yaml)
are hash-verified against `scripts/python/rosdistro_reference.lock.json`.
ROSDISTRO_INDEX_URL points to this local snapshot. Platform support and package
versions in it are not modified, and no check/report category is excluded.

Actual doctor checks, doctor --report, wtf checks and wtf --report run on both
boards while the real DSoftBus ROS fixture remains live. Every check command
must report all five checks passed. Each report must contain all seven report
categories, the rmw_mdds identity, Jazzy metadata, the four live data topics'
exact publisher/subscriber counts and their expected compatible QoS pairs.
Missing entry-point imports or failed report functions are rejected explicitly.

Run `cli_doctor_20260907_01` passed with all CLI and supervisor exits zero.
Partial manifest SHA-256:
`27741e1e5a72db7d6800de6a6f164b930ecf338bd2cdb5149f6dd2d9eb75468d`.
Four output-oracle tests, three dependency extraction tests and eight actual
receipt adversaries passed; the first two groups were introduced RED before
implementation. Eleven base ROS receipt tests also passed. Native broker
provenance and the base cross-board data/service/graph checks remain required.

The commands retain their warnings: some pinned port packages are older than
the upstream snapshot, custom MDDS packages are not listed there, and the CLI
fixture deliberately contains unpaired cli_source/cli_sink endpoints. Default
doctor semantics do not count warnings as failures; this evidence does not
claim a zero-warning environment or that all installed packages are latest.

```bash
python scripts/mdds_e2e/check_doctor_receipt.py \
  ohos_test_logs/ros_broker/cli_doctor_20260907_01
```

Doctor and wtf added two cases, bringing the ledger to 72/98 at that point.

## Doctor hello and wtf hello

Mode `MDDS_ROS_CLI_BATCH=hello` stages the complete source Python doctor package
with a file/hash inventory. Each command uses a nonce-bound host-id, its own
run topic, an explicit multicast port and the verified Ethernet interface.
The source implementation supports repeated hostnames and rejects malformed
hello payloads without terminating its receive loop.

Acceptance requires nonzero peer ROS and UDP counts in the same summary;
neither lane substitutes for the other. An independent ROS observer checks
the exact peer identity/payload, publisher node names bound to actual CLI PIDs,
type/hash/GID metadata, and node/endpoint withdrawal. Native MDDS/RMW library
hashes and the DSoftBus broker remain mandatory. The CLI's two intentional
diagnostic UDP sockets are recorded separately from the broker's no-UDP proof.
Both boards must observe valid output before the host sends stop markers;
the actual controlled SIGINT return code is retained and emergency cleanup fails.

`cli_hello_20260907_03` passed both commands on both boards, including daemon
shutdown/port isolation and the base ROS fixture. Partial manifest SHA-256:
`156ca135e832bae5d62316fb5529bea38d7597d9981efd79b2e1effcbcf0b41b`.
Eight source unit tests, five summary tests, twelve controlled-stop contract
tests and eight actual-receipt adversaries passed. Run 01's transient daemon
port failure remains an unresolved stability observation; run 02's staging
failure was recovered only after confirming no test PID records existed.

```bash
python scripts/mdds_e2e/check_hello_receipt.py \
  ohos_test_logs/ros_broker/cli_hello_20260907_03
```

The aggregate CLI/graph/transport ledger is now 74/98. Remaining commands, full
graph semantics, stability observations and unified deployment are unfinished.
