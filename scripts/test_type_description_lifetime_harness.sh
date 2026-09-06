#!/usr/bin/env bash
# Every HDC access is forbidden in this host-only contract test.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
tmp=$(mktemp -d)
export HDC="$tmp/no_board_hdc"
printf '#!/usr/bin/env bash\nprintf CALLED > "%s/called"\nexit 99\n' "$tmp" > "$HDC"
chmod +x "$HDC"
source scripts/run_mdds_type_description_lifetime.sh
lifetime_parse_args --board board_test --domain 52 --wait-seconds 15
[[ "$LIFETIME_BOARD" == board_test && "$LIFETIME_DOMAIN" == 52 && "$LIFETIME_WAIT_SECONDS" == 15 ]] || { echo 'FAIL: requested board/domain/wait ignored'; exit 1; }
for value in 0 233 abc; do
  if lifetime_parse_args --domain "$value"; then echo 'FAIL: unsafe domain accepted'; exit 1; fi
done
if lifetime_parse_args --board 'bad;board'; then echo 'FAIL: unsafe board accepted'; exit 1; fi
if lifetime_parse_args --wait-seconds 9999; then echo 'FAIL: unbounded wait accepted'; exit 1; fi
if lifetime_parse_args --unknown; then echo 'FAIL: unknown option accepted'; exit 1; fi
[[ ! -e "$tmp/called" ]] || { echo 'FAIL: host test touched HDC'; exit 1; }
echo 'TYPE_DESCRIPTION_HOST_SHELL PASS no_board_calls=true'
rm -f -- "$tmp/no_board_hdc"
rmdir -- "$tmp"
