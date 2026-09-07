# Hidden entities and native graph names

Design: create a visible and a hidden node in each source context. The visible
node owns public resources and additional `_secret` topic/service/action
resources. The hidden node owns its own topic/service/action group. Source
readiness requires the complete graph, including native transport names and
raw publisher/subscriber queries by node. Sources remain alive while actual
CLI processes query default and include-hidden views, then retire before final
daemon/no-daemon checks.

The independent contract in `hidden_graph_contract.py` requires exact scoped
sets. Default output also rejects hidden tokens anywhere in the returned CLI
view. no-demangle API evidence must retain `rt`, `rq` and `rr` names and message
types, including service/action plumbing; hiding is a CLI view operation.

Tests were supplied first. Three CLI action-list tests and three hidden graph
contract tests failed before implementation. Action list was inconsistent with
node info: it listed hidden actions without an include-hidden option. The fix
adds `--include-hidden-actions` and default filtering using rclpy's existing
hidden-token predicate. The underlying graph API remains complete.

Actual HDC run `graph_hidden_20260907_01` passed both RK3588A boards over the
real DSoftBus broker. Default / complete scoped CLI counts on each board were:

| View | Default | Include hidden |
| --- | ---: | ---: |
| Nodes | 2 | 4 |
| Topics | 2 | 18 |
| Services | 4 | 28 |
| Actions | 2 | 6 |

All 74 native transport topics and the complete raw by-node endpoint views
matched the contract. Eleven evidence adversaries, 18 generic gate tests and
the enclosing 11 broker receipt adversaries passed. Normal cleanup completed.

- Final manifest SHA-256:
  `223c39d66277a25626eb56b321a1b56818d02ecf14bd16852efb390f0af0fb37`.
- Hidden graph receipt SHA-256:
  `2cd690dbe2397b194d259d5a6e504985084985958a08358f11bc792a77130b14`.
- Frozen CLI overlay manifest SHA-256:
  `cf84ee0b8e52c4f06ab87d449df7bedc0c1d0b34c1a12323bf326fc14f301510`.

The overlay now includes ros2action as well as ros2cli and ros2multicast, so
the action-list behavior is bound to the tested source. Shared-prefix rollout
is part of the later unified release gate.

Coverage is 90/98. Churn, failure/recovery, isolation,
transport negatives, advanced QoS and unified-release validation remain open.
Gateway is outside the current goal; no complete graph/release is claimed.
