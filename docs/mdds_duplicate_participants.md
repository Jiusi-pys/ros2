# Same-name nodes across participants

Design and test contract, before implementation: each RK3588A creates two
independent rclpy contexts. All four nodes use exactly the same name and
namespace. Each context owns its own topic publisher/subscriber and service
server/client; resources identify the board and context index. The observer
must see four node entries and the union of all four owners' endpoints.
Publisher GIDs must have four distinct participant prefixes.
The service union includes rclpy's default `~/get_type_description` service;
all four owners share that service name, while their explicit RPC names differ.

After both boards have completed the first graph/data phase, close index 1
on each board, including its executor and context. The next graph must have
exactly two same-name entries, no retired endpoints, and unchanged survivor
GIDs. Both phases require three exact nonce-bound cross-board messages per
live context and a completed peer service request. Source barriers hold the
phases until both boards have recorded their observations.

`test_duplicate_graph_contract.py` supplies exact independent fixtures and
rejects collapsed multiplicity, shared participant identities, stale endpoint
sets, replaced survivor GIDs and missing by-node service ownership.

The contract module was absent for the first RED run. The first physical run,
`graph_duplicate_20260907_01`, exposed an incomplete fixture expectation: the
default type-description service had been omitted. It remains enabled, and
the independent expected set was expanded to include its exact name/type.
The corrected fixture failed against the old contract, then passed with the
complete expectation. No graph query or middleware behavior was weakened.
Both boards' failed-run logs and successful owned CLI cleanup were preserved.

`graph_duplicate_20260907_02` completed both native graph/data phases and the
baseline broker checks, with ROS child exits of zero. Formal acceptance still
failed because the supervisor's graph-case allowlist omitted the new case:
post-exit actual-argv and terminal markers were absent. Receipt tests reproduce
this rejection. The allowlist was corrected for a fresh complete run; no old
terminal markers or accepted manifest were fabricated.

Final HDC run `graph_duplicate_20260907_03` passed with Bash exit 0 and normal
ROS/CLI process exits on both boards. Each observer saw 4 then 2 same-name
nodes, publisher/subscriber unions of 4 then 2 topics, service unions of 5 then
3 names (including the shared type-description service), and 4 then 2 client
names. Four distinct participant prefixes became two unchanged survivor GIDs.
Each board received 6 exact peer messages before retirement and 3 afterward,
with 2 then 1 completed peer RPCs. Both owned CLI cleanups completed.

Six contract tests, six synthetic evidence tests, twelve adversaries against
the final physical receipt, eighteen generic acceptance tests and eleven
enclosing broker adversaries passed. The feature changes the test harness and
acceptance evidence; the existing native MDDS/RMW implementation passed it.

- Final manifest SHA-256:
  `cdc7b5b64ddba0867af1552a57d0e250395c3cc8b36ce1df7be440abe3516f8e`.
- Duplicate-node receipt SHA-256:
  `79e9a7d3a0d2049a2282229937de927a4ec8b6f7dd7bae8cbf7db9e621a04e3b`.

Aggregate coverage is 92/98, across different frozen runs. Reconnect, isolation, transport negative cases, advanced functionality and a
unified release remain open. Gateway remains outside the current goal.
