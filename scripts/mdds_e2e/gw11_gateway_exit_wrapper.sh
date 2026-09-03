#!/bin/sh
# Test-only GW-11 wrapper.  The ordinary gateway launcher execs the gateway,
# which is appropriate for cleanup but leaves no process exit-code record on
# HDC (whose own shell status is not trustworthy).  This wrapper is deployed
# only for the cap-negative gate and writes one create-only, run-bound result
# after the real gateway exits.  It is not used by production deployments.
set -u

usage()
{
  echo "usage: $0 <status> <run-id> <nonce>" >&2
}

[ "$#" -eq 3 ] || { usage; exit 64; }
status=$1
run_id=$2
nonce=$3

case "$status" in
  /data/local/tmp/ros2/mdds_gw_runs/*/gw11_gateway_exit.status) ;;
  *) echo "GW11_GATEWAY_EXIT_WRAPPER invalid-status-path" >&2; exit 64 ;;
esac
case "$run_id" in ''|*[!A-Za-z0-9_-]*) usage; exit 64 ;; esac
case "$nonce" in ''|*[!A-Za-z0-9_-]*) usage; exit 64 ;; esac

status_dir=${status%/*}
if ! test -d "$status_dir" || test -L "$status_dir" || test -e "$status" || test -L "$status"; then
  echo "GW11_GATEWAY_EXIT_WRAPPER invalid-status-target" >&2
  exit 65
fi

write_status()
{
  rc=$1
  state=$2
  case "$rc" in ''|*[!0-9]*) rc=255 ;; esac
  line="GW11_GATEWAY_EXIT RUN_ID=$run_id NONCE=$nonce STATE=$state RC=$rc"
  if (umask 077; set -C; printf '%s\n' "$line" > "$status") 2>/dev/null; then
    printf '%s\n' "$line"
    return 0
  fi
  # A signal can arrive during output.  A matching, already-created record is
  # still proof for this exact run; any differing content fails closed.
  if test -f "$status" && test ! -L "$status" && grep -Fqx "$line" "$status"; then
    printf '%s\n' "$line"
    return 0
  fi
  echo "GW11_GATEWAY_EXIT_WRAPPER status-write-failed" >&2
  return 1
}

child_pid=''
stop_child()
{
  signal_rc=$1
  if [ -n "$child_pid" ] && kill -0 "$child_pid" 2>/dev/null; then
    kill "$child_pid" 2>/dev/null || true
    elapsed=0
    while kill -0 "$child_pid" 2>/dev/null && [ "$elapsed" -lt 5 ]; do
      sleep 1
      elapsed=$((elapsed + 1))
    done
    if kill -0 "$child_pid" 2>/dev/null; then
      kill -9 "$child_pid" 2>/dev/null || true
    fi
    wait "$child_pid" 2>/dev/null || true
  fi
  child_pid=''
  write_status "$signal_rc" SIGNAL || true
  exit "$signal_rc"
}
trap 'stop_child 143' TERM
trap 'stop_child 130' INT
trap 'stop_child 129' HUP

/data/local/tmp/ros2/lib/mdds_gateway/mdds_gateway \
  -c /data/local/tmp/ros2/mdds_e2e/mdds_gateway_test.conf &
child_pid=$!
wait "$child_pid"
gateway_rc=$?
child_pid=''
write_status "$gateway_rc" EXIT || exit 66
exit "$gateway_rc"
