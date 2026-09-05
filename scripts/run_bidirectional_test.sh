#!/usr/bin/env bash
# Compatibility entry point.  The legacy demo talker/listener gate used fixed
# logs and accepted any stale "I heard" line.  It is intentionally retired in
# favour of the ownership-fenced, hash-bound production-domain smoke gate.
set -euo pipefail
cd "$(dirname "$0")/.."

if [ "$#" -ne 0 ]; then
  echo "ERROR: duration/legacy arguments are no longer accepted." >&2
  echo "Use MDDS_RUN_ID/MDDS_RUN_NONCE with run_domain0_chatter_smoke.sh." >&2
  exit 2
fi

: "${MDDS_RUN_ID:=bidir_$(date +%Y%m%dT%H%M%S)_${RANDOM}}"
: "${MDDS_RUN_NONCE:=n${RANDOM}p${RANDOM}x$$}"
export MDDS_RUN_ID MDDS_RUN_NONCE
echo "NOTICE: run_bidirectional_test.sh now delegates to the hardened domain-0 gate."
exec "$PWD/scripts/mdds_e2e/run_domain0_chatter_smoke.sh"
