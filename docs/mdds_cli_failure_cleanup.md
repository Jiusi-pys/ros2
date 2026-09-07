# CLI daemon cleanup after worker failure

Problem: the ROS CLI daemon double-forks and outlives the CLI worker's process
group. If that worker is killed before its `finally` block runs, the previous
outer cleanup removed tracked workers/brokers but left the daemon alive. A
later isolated run then correctly rejected the foreign old-run daemon.

Design and tests were supplied before implementation:

1. Stop all tracked run-owned workers. If any identity remains unresolved,
   retain activity locks and do not proceed to daemon cleanup.
2. Execute only the frozen, hash-verified cleanup guard. Select daemons by
   exact domain/argv, broker root, loaded MDDS/RMW library paths and PID/start.
   Recheck ownership before signalling. Foreign processes are preserved.
3. Rescan for remaining owned daemons; record the result and verify port reuse
   when no foreign daemon exists. Release locks only after cleanup succeeds.

The runtime test mode `daemon_abort` starts real daemons on both boards and
kills only the two verified worker identities with SIGKILL. This is a failure
injection mode, not a functional acceptance case. The standalone checker can
recover a RED's exact run-owned daemons after recording the failure.

TDD and actual RK3588A evidence:

- `daemon_abort_red_20260907`: both workers were killed; one daemon survived
  per board. The checker reported failure and recovered those exact identities.
- Selection tests initially failed; all five now pass, including foreign
  preservation and identity changes before signal. Seven existing ownership
  checks and three cleanup-order checks pass. The order tests require locks
  to remain held after worker or daemon cleanup failure.
- `daemon_abort_final_20260907`: exact Bash exit 42 was explicitly recorded;
  both worker exits were -9. Both selected daemon PID/start pairs matched the
  ready records, were retired by the outer cleanup, and no owned daemon
  remained. Ports were reusable and lock release succeeded.
- Final abort verification SHA-256:
  `2d8beb4250cb742ae45b88892851b20738b114c06705fb0ed571dda3b7cc8e64`.

The initial Windows invocation reported generic exit 1 for nonzero Bash exits.
The final invocation explicitly captures `$LASTEXITCODE`; it does not relabel
the earlier wrapper results as exact exit 42.

Normal regression `cleanup_normal_20260907_01` passed the actual DSoftBus
endpoint/QoS and core broker matrix, with exact Bash exit 0. Both cleanup
reports selected no daemon, left no owned process and verified reusable ports.
All 16 endpoint receipt adversaries passed.

- Normal manifest SHA-256:
  `2c023c563fff70c5afbfd81eb93c68f1d98aa40b11fed37fbae7eaa45e7ee5b4`.
- Normal cleanup verification SHA-256:
  `00d516831ab7c0a9c2527846eecc06d3f1c1b3c4bb11ee75d76d1ca66f654869`.

Coverage remains 88/98. This closes the tested outer CLI daemon cleanup gap;
remaining graph/transport, advanced QoS and unified release gates are open.
