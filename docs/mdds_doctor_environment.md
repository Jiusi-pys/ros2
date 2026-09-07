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

Remaining diagnostic work must run full checks and reports against live
fixtures, verify the expected module count and rmw_mdds identity, and inspect
all failures. The hello test also needs distinct peer identities because both
boards currently report hostname localhost. Diagnostic multicast must remain
separate from proof that ROS middleware traffic uses DSoftBus.
