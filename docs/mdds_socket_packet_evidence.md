# Socket byte evidence for real ROS transport

Design and tests preceded implementation. A noninstalled SDK wrapper fixture
calls the real SendBytes/Listen/Bind/BindAsync functions. It captures a
SendBytes buffer only after the actual SDK result returns and captures received
bytes at the actual OnBytes callback before forwarding to the unchanged native
listener. Other callbacks remain unchanged. The production daemon gains no
default payload capture behavior.

Capture is limited to packets containing the fresh run/nonce payload marker.
Memory is allocated before SDK startup; borrowed callback bytes are copied into
a bounded buffer. Oversize matching packets, buffer exhaustion or incomplete
output must fail acceptance, never create a partial-success trace. The trace
retains direction, SDK fd, native result and full packet bytes.

Host verification decodes the broker envelope and MDDS DATA identity/CDR payload,
matches successful sender bytes to exact receiver callback bytes, binds the writer
GUID to the observed ROS publisher and requires the exact ROS receiving result.
Socket/Listen/Bind evidence and native process/library identities remain required.
Each direction receives its own receipt. Missing callbacks or payloads cannot
be replaced by packet lengths, hashes alone, or an aggregate PASS message.

`sdk_packet_capture_cases.inc` supplied borrowed-lifetime, native-result,
unrelated-packet and overflow/oversize tests before implementation. The missing
header build RED is retained in `sdk_capture.red.log`; the host decoder's
missing-module RED is retained in `sdk_packet_decoder.red.log`. The native
`test_broker_remote_daemon` target passed all 17 tests on RK3588A in
`sdk_capture_20260908_01`, including SDK-wrapper delegation and callback forwarding.
Archive SHA256:
`2eb41f837abe5a4155e9d91f91e5f51090ca04b6fa1943f8b43b0a3c7a2d2571`.

Physical HDC run `socket_packets_20260908_02` passed:

- Board A: `3e01ff55454d202020104033bf453b00`.
- Board B: `3e01ff55454d202020104433991c3b00`.
- Both directions: ten alpha String samples across two phases, each bound to
  its writer GUID/epoch/sequence, exact SDK packet bytes and ROS callback.
- Each board: 37 captured records, 10329 bytes, no capture failure. Retransmission
  records remain in the original capture; acceptance requires at least one exact
  successful send/receive pair for every expected message.
- Real Socket/Listen and BindAsync/OnBind established the native channel. Both
  ROS and both broker processes exited zero; broker resources were zero at stop.

The initial `_01` run fetched the capture during staging, before any daemon
started, and exited 1. Collection was moved after native completion; the failed
run remains archived and has no acceptance receipt. The receipt adversaries
then exposed three missing provenance rechecks (binary hash, process start,
owned UDP snapshot). `verify_socket_packets.py` now revalidates the original
baseline records and compares them with the saved report before emitting either
receipt. `socket_receipt.red.log` retains those failures. All 17 receipt tests,
four decoder tests, 18 generic acceptance tests and 11 baseline broker
adversaries passed; the same unmodified native run passed the stronger verifier.

Frozen evidence hashes:

- SDK fixture: `43ab2b707e2b99ca6e55b5b3a4ee47661aec061b377774eac9b4536ea6ac728d`.
- MDDS: `c35102eedb8c94402dfd49b8129114528da0f379e31bdd17e612ff08abe4b1db`.
- RMW: `a46c8b16ebcbc7e9cd249d60a77384f6bcec493cb3be9987659ea5b199db1dbf`.
- A-to-B receipt: `3dca5acbfb68b572800eb78c2b37026490f3fc6fe57d909ba81e11d81fc88fcd`.
- B-to-A receipt: `035796d0136ee623bfbd8a8c2629455bce2831bf787c9a3249c996db69349f95`.
- Partial manifest: `323d41ed38f4a1a625d6142ef253b5e9092cbdd270860df6764eec390cb1594c`.

Reproduce with `MDDS_RUN_ID=<fresh> MDDS_ROS_PROFILE_MODE=implicit
MDDS_ROS_CLI_BATCH=none MDDS_ROS_SDK_TRACE=1 bash scripts/run_mdds_broker_ros.sh`.
Then run `check_socket_packet_receipt.py` with the run directory. Capturing
fragmented large samples is outside this small-String fixture's scope.

Inventory coverage is 97/98 across frozen historical builds, not one release.
The forced-failure/no-UDP-fallback case remains separate and open. Advanced
RMW API/liveliness work and the unified release also remain open; gateway is
outside the current goal. No push or agentic-review is claimed by this feature.
