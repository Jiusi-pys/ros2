# GW-ISO: Board-B 192.168.8.0/24 isolation gate

`gw_iso` is an explicit, maintenance-window-only gateway topology gate. It is
not selected by the default run or by `all` because it temporarily removes one
live Board-B address.

```bash
# No HDC/device action; validates the fixed-scope safety contract and helpers.
./scripts/run_mdds_gw.sh --validate-gw-iso-contract

# Only after the maintenance window has been approved.
MDDS_RUN_ID=gw_iso_<unique_id> ./scripts/run_mdds_gw.sh gw_iso
```

The only network mutation is an exact deletion of
`192.168.8.111/24` from Board B's `wlan0`. The runner refuses to act if the
target is absent, duplicated, or if another `192.168.8.x` address is present.
It never brings an interface down, changes a route manually, or changes Board
A. Restoration adds precisely the same CIDR back to the same interface.

Before the deletion the runner creates a Board-B timer under the current
run-owned directory. Its record contains `RUN_ID`, nonce, timer PID and
process-start identity. The timer independently restores the fixed address
after 180 seconds if the host cannot reach normal cleanup. Normal and signal
cleanup both restore the address first and only signal a timer whose PID and
start identity still match; a reused PID is never killed. A failure to prove
rollback/disarm retains the activity locks for operator recovery.

The new run directory contains these topology inputs and outputs:

- `network_isolation/pc_pre_network.log` and `pc_post_network.log`: PC IPv4
  addresses and routes.
- `board_a_pre_network.log`, `board_b_pre_network.log`,
  `board_b_isolated_network.log`, `board_a_post_network.log`,
  `board_b_post_network.log`, and `board_b_restored_network.log`: board IPv4
  addresses, routes, rules, neighbours, route lookups, and interface counters.
- `board_b_{pre,isolated,restored}_state.log`: exact address and route checks.
- `tcp_{pre_a,pre_b,post_b}.log`: bounded direct-PC TCP controls. The PC
  listener is bound only to `192.168.8.101`.
- `rollback_{arm,restore}.log` and `rollback_identity.txt`: rollback ownership
  and restoration proof.
- `eth1_counter_delta.log`: Board-B transmit and Board-A receive deltas across
  the DSoftBus leg.

The isolated state is accepted only when Board B has no `192.168.8.x` address,
`ip route get 192.168.8.101` reports an explicit unreachable result, and its
route to Board A (`192.168.77.201`) remains through `eth1` with source
`192.168.77.202`. It also requires the Board-B TCP probe to the PC listener to
fail. Before and after the temporary change, the same bounded listener proves
the expected direct source addresses.

After isolation, the gate sends 20 CRC-checked 1 KiB samples from Board B via
MDDS/DSoftBus to the gateway on Board A and CycloneDDS to the PC. The PC must
report exact `20/20`, zero loss/reorder/CRC errors, while both board and gateway
logs prove DSoftBus Socket/Bytes calls and no MDDS UDP backend. This is scoped
evidence that MDDS uses the DSoftBus Socket/Bytes API; it does not make a claim
about DSoftBus's internal bearer protocol or every possible PC route outside
the captured configuration.
