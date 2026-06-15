#!/usr/bin/env bash
# Two-board RK3588A ROS 2 full-feature test over FastDDS + CycloneDDS.
# Usage: run_cross_board_full_matrix.sh <device_a> <device_b>
#
# Proves two physical boards transport ROS 2 traffic and that all ROS 2
# functions work, across three DDS modes:
#   fastdds    - both boards FastDDS  (/usr/local/bin/ros2)
#   cyclonedds - both boards CycloneDDS (/data/local/tmp/ohos-cyc/ros2-cyclone)
#   interop    - A=FastDDS <-> B=CycloneDDS (cross-vendor RTPS, pub/sub-class)
# plus per-board LOCAL coverage of non-distributable features under both DDS.
#
# Emits RESULT|<lane>|PASS/FAIL/NA|<evidence> and a summary. Self-contained:
# drives the already-deployed FastDDS underlay + CycloneDDS overlay via the two
# launchers; makes no on-device build/deploy changes.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
DEVA="${1:?device A id}"; DEVB="${2:?device B id}"
FASTROS=/usr/local/bin/ros2
CYCROS=/data/local/tmp/ohos-cyc/ros2-cyclone
IPA=192.168.77.10; IPB=192.168.77.11
REPORT="${REPORT:-/tmp/xb_full_matrix.out}"

. "${HERE}/crossboard_lanes.sh"
. "${HERE}/local_features.sh"

log() { echo "$@"; }

preflight() {
  log "===== PREFLIGHT ====="
  local got; got="$(hdc list targets 2>/dev/null)"
  echo "$got" | grep -q "$DEVA" && echo "$got" | grep -q "$DEVB" || { log "FATAL: both devices must be connected"; hdc list targets; exit 1; }
  # eth1 static IPs (idempotent) + link up
  hdc -t "$DEVA" shell "ip link set eth1 up; ip addr show eth1 | grep -q ${IPA} || ip addr add ${IPA}/24 dev eth1" >/dev/null 2>&1
  hdc -t "$DEVB" shell "ip link set eth1 up; ip addr show eth1 | grep -q ${IPB} || ip addr add ${IPB}/24 dev eth1" >/dev/null 2>&1
  local ploss; ploss="$(hdc -t "$DEVA" shell "ping -c2 -W2 ${IPB} 2>&1 | grep -o '[0-9]*% packet loss'" 2>/dev/null | tr -d '\r')"
  log "eth1 A->B: ${ploss:-unknown}"
  for D in "$DEVA" "$DEVB"; do
    hdc -t "$D" shell "[ -x ${FASTROS} ] && echo ok" 2>/dev/null | grep -q ok || { log "FATAL: ${FASTROS} missing on ${D}"; exit 1; }
    hdc -t "$D" shell "[ -f ${CYCROS} ] && echo ok" 2>/dev/null | grep -q ok || log "WARN: ${CYCROS} missing on ${D} (cyclone lanes will be NA)"
  done
  log "preflight OK"
}

mode() { # tag launchA launchB dom  fn
  local tag="$1" la="$2" lb="$3" dom="$4" fn="$5"
  log ""; log "===== MODE ${tag} (A=${la##*/} B=${lb##*/} domain ${dom}) ====="
  TAG="$tag" DEVA="$DEVA" DEVB="$DEVB" LAUNCH_A="$la" LAUNCH_B="$lb" DOM="$dom" "$fn"
}

local_pass() { # tag dev launcher dom
  local tag="$1" dev="$2" launch="$3" dom="$4"
  log ""; log "===== LOCAL ${tag} (board ${dev:0:8} ${launch##*/} domain ${dom}) ====="
  TAG="$tag" DEV="$dev" LAUNCH="$launch" DOM="$dom" run_local_features
}

main() {
  : > "$REPORT"
  { preflight

    # --- cross-board, three DDS modes ---
    mode fastdds    "$FASTROS" "$FASTROS" 100 run_crossboard_lanes
    mode cyclonedds "$CYCROS"  "$CYCROS"  112 run_crossboard_lanes
    # interop: cross-vendor RTPS pub/sub-class only (RPC does not interoperate)
    log ""; log "===== MODE interop (A=fastdds B=cyclonedds domain 124; pub/sub-class only) ====="
    TAG=interop LAUNCH_A="$FASTROS" LAUNCH_B="$CYCROS" DOM=124 run_crossboard_pubsub_lanes

    # --- per-board local (non-distributable) features under both DDS ---
    local_pass local_fastdds_A    "$DEVA" "$FASTROS" 130
    local_pass local_fastdds_B    "$DEVB" "$FASTROS" 140
    local_pass local_cyclonedds_A "$DEVA" "$CYCROS"  150
    local_pass local_cyclonedds_B "$DEVB" "$CYCROS"  160
  } 2>&1 | tee "$REPORT"

  # --- summary ---
  log ""; log "===== SUMMARY ====="
  local P F N
  P=$(grep -c '|PASS|' "$REPORT"); F=$(grep -c '|FAIL|' "$REPORT"); N=$(grep -c '|NA|' "$REPORT")
  for grp in fastdds_ cyclonedds_ interop_ local_fastdds local_cyclonedds; do
    log "  ${grp}: PASS=$(grep "RESULT|${grp}" "$REPORT" | grep -c '|PASS|') FAIL=$(grep "RESULT|${grp}" "$REPORT" | grep -c '|FAIL|') NA=$(grep "RESULT|${grp}" "$REPORT" | grep -c '|NA|')"
  done
  log "  TOTAL: PASS=${P} FAIL=${F} NA=${N}"
  log "  FAILURES:"; grep '|FAIL|' "$REPORT" | sed 's/^/    /' || true
  log "FULL_MATRIX_DONE PASS=${P} FAIL=${F} NA=${N}"
}
main
