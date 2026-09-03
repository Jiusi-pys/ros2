#!/bin/sh

process_state()
{
    pid=$1
    expected_start=$2
    stat_line=$(cat "/proc/$pid/stat" 2>/dev/null) || stat_line=
    if [ -z "$stat_line" ]; then
        printf GONE
        return
    fi
    stat_tail=${stat_line##*) }
    set -- $stat_tail
    state=$1
    shift 19
    actual_start=$1
    if [ "$actual_start" != "$expected_start" ]; then
        printf REUSED
    elif [ "$state" = Z ]; then
        printf GONE
    else
        printf LIVE
    fi
}

process_group()
{
    stat_line=$(cat "/proc/$1/stat" 2>/dev/null) || return
    stat_tail=${stat_line##*) }
    set -- $stat_tail
    printf '%s' "$3"
}

process_start()
{
    stat_line=$(cat "/proc/$1/stat" 2>/dev/null) || return
    stat_tail=${stat_line##*) }
    set -- $stat_tail
    shift 19
    printf '%s' "$1"
}

group_member_count()
{
    wanted=$1
    count=0
    for stat_path in /proc/[0-9]*/stat; do
        test -r "$stat_path" || continue
        member_pid=${stat_path#/proc/}
        member_pid=${member_pid%/stat}
        member_group=$(process_group "$member_pid")
        if [ "$member_group" = "$wanted" ]; then
            count=$((count + 1))
        fi
    done
    printf '%s' "$count"
}

cleanup_main()
{
    record_path=$2
    run_id=$3
    nonce=$4
    tag=$5
    supervisor_pid=$6
    supervisor_start=$7
    process_group_id=$8
    child_pid=$9
    child_start=${10}
    term_polls=${11:-20}
    for numeric_value in "$supervisor_pid" "$supervisor_start" "$process_group_id" "$child_pid" "$child_start" "$term_polls"; do
        case "$numeric_value" in
            ''|*[!0-9]*) printf 'D0_REMOTE_CLEANUP result=INVALID\n'; return 70 ;;
        esac
    done
    [ "$supervisor_pid" = "$process_group_id" ] || {
        printf 'D0_REMOTE_CLEANUP result=INVALID_GROUP_LEADER\n'
        return 70
    }
    [ "$term_polls" -ge 1 ] && [ "$term_polls" -le 20 ] || {
        printf 'D0_REMOTE_CLEANUP result=INVALID_POLLS\n'
        return 70
    }
    expected="D0_LAUNCH_RECORD RUN_ID=$run_id NONCE=$nonce TAG=$tag PID=$supervisor_pid START=$supervisor_start PGID=$process_group_id CHILD_PID=$child_pid CHILD_START=$child_start"
    test -f "$record_path" && test ! -L "$record_path" && grep -Fqx "$expected" "$record_path" || {
        printf 'D0_REMOTE_CLEANUP result=RECORD_MISMATCH\n'
        return 70
    }
    cleanup_group=$(process_group $$)
    [ "$cleanup_group" != "$process_group_id" ] || {
        printf 'D0_REMOTE_CLEANUP result=SELF_GROUP_COLLISION\n'
        return 70
    }

    supervisor_state=$(process_state "$supervisor_pid" "$supervisor_start")
    child_state=$(process_state "$child_pid" "$child_start")
    case "$supervisor_state:$child_state" in
        *REUSED*) printf 'D0_REMOTE_CLEANUP result=PID_REUSED\n'; return 70 ;;
    esac
    if [ "$supervisor_state" = LIVE ] && [ "$(process_group "$supervisor_pid")" != "$process_group_id" ]; then
        printf 'D0_REMOTE_CLEANUP result=SUPERVISOR_GROUP_MISMATCH\n'
        return 70
    fi
    if [ "$child_state" = LIVE ] && [ "$(process_group "$child_pid")" != "$process_group_id" ]; then
        printf 'D0_REMOTE_CLEANUP result=CHILD_GROUP_MISMATCH\n'
        return 70
    fi
    members=$(group_member_count "$process_group_id")
    if [ "$supervisor_state" = GONE ] && [ "$child_state" = GONE ] && [ "$members" -eq 0 ]; then
        printf 'D0_REMOTE_CLEANUP result=GONE\n'
        return 0
    fi

    if [ "$supervisor_state" = LIVE ]; then
        kill -TERM "$supervisor_pid" 2>/dev/null || true
    else
        kill -TERM -"$process_group_id" 2>/dev/null || true
    fi
    poll=0
    while [ "$poll" -lt "$term_polls" ]; do
        supervisor_state=$(process_state "$supervisor_pid" "$supervisor_start")
        child_state=$(process_state "$child_pid" "$child_start")
        case "$supervisor_state:$child_state" in
            *REUSED*) printf 'D0_REMOTE_CLEANUP result=PID_REUSED\n'; return 70 ;;
        esac
        members=$(group_member_count "$process_group_id")
        if [ "$supervisor_state" = GONE ] && [ "$child_state" = GONE ] && [ "$members" -eq 0 ]; then
            printf 'D0_REMOTE_CLEANUP result=STOPPED\n'
            return 0
        fi
        sleep 0.5
        poll=$((poll + 1))
    done

    supervisor_state=$(process_state "$supervisor_pid" "$supervisor_start")
    child_state=$(process_state "$child_pid" "$child_start")
    case "$supervisor_state:$child_state" in
        *REUSED*) printf 'D0_REMOTE_CLEANUP result=PID_REUSED\n'; return 70 ;;
    esac
    if [ "$supervisor_state" = LIVE ] && [ "$(process_group "$supervisor_pid")" != "$process_group_id" ]; then
        printf 'D0_REMOTE_CLEANUP result=SUPERVISOR_GROUP_MISMATCH\n'
        return 70
    fi
    if [ "$child_state" = LIVE ] && [ "$(process_group "$child_pid")" != "$process_group_id" ]; then
        printf 'D0_REMOTE_CLEANUP result=CHILD_GROUP_MISMATCH\n'
        return 70
    fi
    kill -KILL -"$process_group_id" 2>/dev/null || true
    poll=0
    while [ "$poll" -lt 20 ]; do
        supervisor_state=$(process_state "$supervisor_pid" "$supervisor_start")
        child_state=$(process_state "$child_pid" "$child_start")
        members=$(group_member_count "$process_group_id")
        if [ "$supervisor_state" = GONE ] && [ "$child_state" = GONE ] && [ "$members" -eq 0 ]; then
            printf 'D0_REMOTE_CLEANUP result=HARD_STOPPED\n'
            return 0
        fi
        sleep 0.1
        poll=$((poll + 1))
    done
    printf 'D0_REMOTE_CLEANUP result=LIVE supervisor=%s child=%s members=%s\n' "$supervisor_state" "$child_state" "$members"
    return 70
}

if [ "${1:-}" = --cleanup ]; then
    [ "$#" -ge 10 ] && [ "$#" -le 11 ] || {
        printf 'D0_REMOTE_CLEANUP result=INVALID_ARGUMENTS\n'
        exit 70
    }
    cleanup_main "$@"
    exit $?
fi

if [ "${1:-}" = --ignore-term-payload ]; then
    trap '' TERM INT HUP
    while :; do
        sleep 1
    done
fi

[ "$#" -eq 8 ] || exit 70
owner_path=$1
record_path=$2
log_path=$3
run_id=$4
nonce=$5
tag=$6
env_prefix=$7
payload=$8
owner_line="D0_RUN_OWNER RUN_ID=$run_id NONCE=$nonce"
test -f "$owner_path" && test ! -L "$owner_path" && grep -Fqx "$owner_line" "$owner_path" || exit 70
mkdir -p "$(dirname "$record_path")" || exit 70
test ! -e "$record_path" || exit 70
test ! -e "$log_path" || exit 70
printf 'D0_RUN_ID=%s\nD0_RUN_NONCE=%s\nD0_LAUNCH_TAG=%s\n' "$run_id" "$nonce" "$tag" > "$log_path" || exit 70
supervisor_start=$(process_start $$)
process_group_id=$(process_group $$)
for numeric_value in "$supervisor_start" "$process_group_id"; do
    case "$numeric_value" in ''|*[!0-9]*) exit 70 ;; esac
done
[ "$process_group_id" = "$$" ] || exit 70

child_pid=
forward_signal()
{
    signal=$1
    if [ -n "$child_pid" ] && kill -0 "$child_pid" 2>/dev/null; then
        kill -"$signal" "$child_pid" 2>/dev/null || true
    fi
}
trap 'forward_signal TERM' TERM
trap 'forward_signal INT' INT
trap 'forward_signal HUP' HUP
sh -c "$env_prefix exec $payload" >> "$log_path" 2>&1 < /dev/null &
child_pid=$!
child_start=$(process_start "$child_pid")
child_group=$(process_group "$child_pid")
for numeric_value in "$child_pid" "$child_start" "$child_group"; do
    case "$numeric_value" in
        ''|*[!0-9]*) kill -KILL "$child_pid" 2>/dev/null || true; wait "$child_pid" 2>/dev/null; exit 70 ;;
    esac
done
[ "$child_group" = "$process_group_id" ] || {
    kill -KILL "$child_pid" 2>/dev/null || true
    wait "$child_pid" 2>/dev/null
    exit 70
}
record_line="D0_LAUNCH_RECORD RUN_ID=$run_id NONCE=$nonce TAG=$tag PID=$$ START=$supervisor_start PGID=$process_group_id CHILD_PID=$child_pid CHILD_START=$child_start"
(set -C; umask 077; printf '%s\n' "$record_line" > "$record_path") 2>/dev/null || {
    kill -KILL "$child_pid" 2>/dev/null || true
    wait "$child_pid" 2>/dev/null
    exit 70
}
test -f "$record_path" && test ! -L "$record_path" && grep -Fqx "$record_line" "$record_path" || exit 70
printf 'D0_REMOTE_OWNER SUPERVISOR_PID=%s SUPERVISOR_START=%s PGID=%s CHILD_PID=%s CHILD_START=%s\n' \
    "$$" "$supervisor_start" "$process_group_id" "$child_pid" "$child_start" >> "$log_path"

wait "$child_pid"
rc=$?
while kill -0 "$child_pid" 2>/dev/null; do
    wait "$child_pid"
    rc=$?
done
printf 'D0_REMOTE_EXIT RUN_ID=%s NONCE=%s TAG=%s RC=%s\n' "$run_id" "$nonce" "$tag" "$rc" >> "$log_path"
exit "$rc"
