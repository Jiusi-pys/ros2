# Native socket audit controls

The no-UDP-fallback case needs actual socket-creation evidence during forced
DSoftBus failure and recovery. `run_mdds_socket_audit.sh` first verifies that
the noninstalled `libmdds_test_socket_audit.so` can detect real IPv4/IPv6
datagrams and can distinguish local/TCP sockets. Each board runs separate
missing-library, positive and zero-datagram children with frozen inputs and
exact PID/start, native exit and library hash records.

The audit counts libc socket attempts and outcomes, including socket flags and
failures. This directly covers the current MDDS UDP backend's `socket()` call.
It is not a general kernel syscall tracer. Any later no-fallback proof must
show MDDS shutdown before the audit's library-finalizer snapshot.

The deployed Python ELF is a launcher which execs itself before CPython starts.
Early controls rejected its two load records; simply making the notification
idempotent did not resolve them. The launcher source and captured ELF confirmed
the exec, so the audit now records the complete pre-exec counters as well. The
lifetime verifier requires exactly load/exec/load/final with zero bootstrap
counts and matching PID/executable. Missing barriers, unexpected resets, wrong
ordering, wrong executable and missing finalization all fail. The earlier
assumption that only one image would load was replaced by complete evidence
for both images, without dropping either interval.

The counter tests preceded implementation (`socket_audit.red.log`). The
lifetime verifier tests also preceded implementation (`socket_lifetime.red.log`).
All unsuccessful controls (`audit_controls_20260908_01`,
`audit_early_20260908_red`, `audit_controls_20260908_02`,
`audit_image_20260908_01`) remain preserved and unaccepted.

Final run `audit_controls_20260908_03` passed on
`3e01ff55454d202020104033bf453b00` and
`3e01ff55454d202020104433991c3b00`. Per board, the positive child produced 7
socket calls including one IPv4 and one IPv6 datagram; the zero child produced
5 calls and zero datagrams. Both included a real invalid-family failure
(`-1`, errno 97). Missing-audit children exited 3 as expected; the other four
children exited 0. Ten receipt adversaries and eight lifetime tests passed.
Native `test_broker_remote_daemon` also passed 20 tests in
`socket_audit_20260908_01`.

- Audit library: `da3beccc8803d1c13f5a8bd85c2291ad07ba1c59ad35fd5a039c9a09e97a413c`.
- Control report: `73ed1f3d9b0c68d493652eac5120eebdd07c5560d26132368c232db56304fdbe`.
- Native unit archive: `a5782c49cd5516a9cb28a9e072858dfa9e83d31c314dacbeb7092b663b8a7dc8`.

Reproduce after building `mdds_test_socket_audit` with
`MDDS_RUN_ID=<fresh> bash scripts/run_mdds_socket_audit.sh`, then run
`check_socket_audit_probe.py` with the resulting evidence directory. A run ID
must fit the runner's 32-character limit. Production files and the shared board
installation are unchanged.

This verifies the audit instrument, not the forced-failure transport case.
That case must still combine explicit `MDDS_TRANSPORT=dsoftbus`, real SDK link
loss, disconnected graph/services, retained local communication, audited zero
datagrams and restored cross-board ROS traffic. Inventory remains 97/98 across
historical builds; advanced API work, unified release and pre-push review remain
open. Gateway is outside the active goal.
