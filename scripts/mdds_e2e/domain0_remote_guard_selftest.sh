#!/bin/sh
# Board-side fault injection for the domain-0 remote supervisor cleanup path.
set -u

[ "$#" -eq 6 ] || exit 64
python_bin=$1
spawn_helper=$2
guard_helper=$3
test_root=$4
run_id=$5
nonce=$6
case "$test_root" in
    /data/local/tmp/ros2/mdds_e2e/guard_selftest_[A-Za-z0-9_-]*) ;;
    *) echo 'D0_REMOTE_GUARD_SELFTEST result=INVALID_ROOT'; exit 64 ;;
esac
case "$run_id:$nonce" in *[!A-Za-z0-9_:-]*) echo 'D0_REMOTE_GUARD_SELFTEST result=INVALID_ID'; exit 64 ;; esac

owner="$test_root/owner"
record="$test_root/record"
log="$test_root/payload.log"
tag=ignore_term
test ! -e "$test_root" && test ! -L "$test_root" || exit 70
(umask 077; mkdir "$test_root") || exit 70
test -d "$test_root" && test ! -L "$test_root" || exit 70
(set -C; umask 077; printf 'D0_RUN_OWNER RUN_ID=%s NONCE=%s\n' "$run_id" "$nonce" > "$owner") 2>/dev/null || exit 70

# The helper is normally transferred by HDC, which does not preserve its
# executable bit.  Invoke the fault payload through /bin/sh just like the
# supervisor itself so the self-test exercises cleanup rather than deployment
# metadata.
payload="sh $guard_helper --ignore-term-payload"
"$python_bin" "$spawn_helper" "$guard_helper" "$owner" "$record" "$log" \
    "$run_id" "$nonce" "$tag" "" "$payload" || exit 70

record_line=
poll=0
while [ "$poll" -lt 50 ]; do
    record_line=$(cat "$record" 2>/dev/null || true)
    [ -n "$record_line" ] && break
    sleep 0.1
    poll=$((poll + 1))
done
supervisor_pid=$(printf '%s\n' "$record_line" | sed -n 's/.* PID=\([0-9][0-9]*\) START=.*/\1/p')
supervisor_start=$(printf '%s\n' "$record_line" | sed -n 's/.* START=\([0-9][0-9]*\) PGID=.*/\1/p')
process_group_id=$(printf '%s\n' "$record_line" | sed -n 's/.* PGID=\([0-9][0-9]*\) CHILD_PID=.*/\1/p')
child_pid=$(printf '%s\n' "$record_line" | sed -n 's/.* CHILD_PID=\([0-9][0-9]*\) CHILD_START=.*/\1/p')
child_start=$(printf '%s\n' "$record_line" | sed -n 's/.* CHILD_START=\([0-9][0-9]*\)$/\1/p')
for numeric_value in "$supervisor_pid" "$supervisor_start" "$process_group_id" "$child_pid" "$child_start"; do
    case "$numeric_value" in
        ''|*[!0-9]*) echo 'D0_REMOTE_GUARD_SELFTEST result=BAD_RECORD'; exit 70 ;;
    esac
done

cleanup_result=$(sh "$guard_helper" --cleanup "$record" "$run_id" "$nonce" "$tag" \
    "$supervisor_pid" "$supervisor_start" "$process_group_id" "$child_pid" "$child_start" 2)
[ "$cleanup_result" = 'D0_REMOTE_CLEANUP result=HARD_STOPPED' ] || {
    printf 'D0_REMOTE_GUARD_SELFTEST result=CLEANUP_FAILED detail=%s\n' "$cleanup_result"
    exit 70
}
test ! -r "/proc/$supervisor_pid/stat" || exit 70
test ! -r "/proc/$child_pid/stat" || exit 70
for stat_path in /proc/[0-9]*/stat; do
    test -r "$stat_path" || continue
    stat_line=$(cat "$stat_path" 2>/dev/null) || continue
    stat_tail=${stat_line##*) }
    set -- $stat_tail
    [ "$3" != "$process_group_id" ] || {
        echo 'D0_REMOTE_GUARD_SELFTEST result=GROUP_REMAINS'
        exit 70
    }
done
rm -f "$owner" "$record" "$log"
rmdir "$test_root" || exit 70
echo 'D0_REMOTE_GUARD_SELFTEST result=PASS hard_stop=verified supervisor=gone child=gone group=gone'
