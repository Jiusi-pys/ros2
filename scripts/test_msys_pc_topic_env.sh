#!/usr/bin/env bash
# Regression for the Git Bash -> native PowerShell boundary used by DS-03.
# It intentionally needs no HDC, ROS installation, or development board.
set -euo pipefail

cd "$(dirname "$0")/.."
source "$PWD/scripts/lib/mdds_msys_env.sh"

topic='/mdds_dsb_sweep_probe'
export MDDS_PC_SWEEP_TOPIC="$topic"

# Use a pre-existing exclusion to exercise the append path rather than only
# the empty-environment case.  This is the same list construction as
# pc_start_sweep_sub in run_mdds_dsb.sh.
export MSYS2_ENV_CONV_EXCL='AN_EXISTING_VARIABLE'
msys_env_conv_excl="$(mdds_append_msys2_env_conv_excl MDDS_PC_SWEEP_TOPIC)"

if [[ "$msys_env_conv_excl" != 'AN_EXISTING_VARIABLE;MDDS_PC_SWEEP_TOPIC' ]]; then
  echo "FAIL: unexpected MSYS2_ENV_CONV_EXCL list: $msys_env_conv_excl" >&2
  exit 1
fi

actual="$(MSYS2_ENV_CONV_EXCL="$msys_env_conv_excl" \
  powershell -NoProfile -NonInteractive -Command '[Console]::Write($env:MDDS_PC_SWEEP_TOPIC)' | tr -d '\r')"
if [[ "$actual" != "$topic" ]]; then
  echo "FAIL: topic mutated across Git Bash -> PowerShell: expected=$topic actual=$actual" >&2
  exit 1
fi

echo "PASS: Git Bash -> PowerShell preserved MDDS_PC_SWEEP_TOPIC=$actual"
