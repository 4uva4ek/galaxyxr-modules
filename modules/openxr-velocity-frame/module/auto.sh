#!/system/bin/sh
set -eu
MODDIR=${0%/*}
STATE=/dev/gxr_openxr_velocity_frame
[ "$(id -u)" = 0 ] || exit 1
awk '$2 == "/dev" && $3 == "tmpfs" {ok=1} END {exit !ok}' /proc/mounts
[ ! -L "$STATE" ] || exit 1
umask 077
mkdir -p "$STATE"
fail() { echo "ERROR: $*"; printf '%s\n' "$*" > "$STATE/failed"; : > "$MODDIR/disable"; exit 1; }
case "${1:-status}" in
 enable|resume) sh "$MODDIR/cold-boot-reset.sh" "$MODDIR" "$STATE" || exit $? ;;
esac
case "${1:-status}" in
 enable)
  [ ! -f "$MODDIR/remove" ] || exit 1
  sh "$MODDIR/auto.sh" disable
  rm -f "$MODDIR/disable" "$MODDIR/auto.pending"
  exec sh "$MODDIR/auto.sh" resume
  ;;
 disable)
  : > "$MODDIR/disable"
  rm -f "$STATE/run"
  count=0
  while [ -d "$STATE/worker" ]; do
    count=$((count+1)); [ "$count" -le 15 ] || { echo 'Worker busy; retry off'; exit 1; }
    sleep 1
  done
  ;;
 resume)
  [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || exit 0
  mkdir "$STATE/worker" 2>/dev/null || exit 0
  if [ -f "$MODDIR/auto.pending" ]; then
    : > "$MODDIR/disable"; rmdir "$STATE/worker"
    echo 'Previous activation unconfirmed; automatic retry blocked'; exit 1
  fi
  : > "$STATE/run"
  nohup sh "$MODDIR/auto.sh" _run > "$STATE/auto.log" 2>&1 < /dev/null &
  ;;
 _run)
  [ -d "$STATE/worker" ] || exit 1
  cleanup() {
    code=$?
    trap - EXIT
    rm -f "$STATE/run"
    [ "$code" = 0 ] || : > "$MODDIR/disable"
    # Discovery failed before any target was patched: there is nothing to
    # restore. Keep records for every attempted write, including failed ones.
    if [ -f "$STATE/restore-needed" ]; then
      if sh "$MODDIR/action.sh" _auto-off >> "$STATE/auto.log" 2>&1; then
        rm -f "$STATE/restore-needed"
      else
        echo 'Restore failed; close patched apps or fully reboot with module disabled'
        : > "$MODDIR/disable"; code=1
      fi
    fi
    for record in "$STATE/worker/"*; do [ ! -f "$record" ] || rm -f "$record"; done
    rmdir "$STATE/worker"
    exit "$code"
  }
  trap cleanup EXIT
  trap 'exit 1' HUP INT TERM
  count=0
  while [ "$(getprop sys.boot_completed)" != 1 ]; do
    [ -f "$STATE/run" ] && [ ! -f "$MODDIR/disable" ] || exit 0
    count=$((count+1)); [ "$count" -le 150 ] || exit 1
    sleep 2
  done
  unavailable=0
  while [ -f "$STATE/run" ]; do
    [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || exit 0
    count=0
    while [ "$(getprop sys.boot_completed)" != 1 ]; do
      [ -f "$STATE/run" ] && [ ! -f "$MODDIR/disable" ] || exit 0
      [ "$count" != 0 ] || echo 'WAITING: Android is restarting; no activation attempted'
      count=$((count+1)); [ "$count" -le 150 ] || fail 'Android restart exceeded five minutes'
      sleep 2
    done
    planned=0
    # Retain the notice until discovery succeeds, including PackageManager's
    # recovery interval after boot_completed becomes 1.
    [ ! -f "$STATE/soft-restart" ] || planned=1
    result=0
    "$MODDIR/bin/gxr-openxr-velocity" list > "$STATE/auto-targets" || result=$?
    if [ "$result" = 3 ]; then
      unavailable=$((unavailable+1))
      [ "$unavailable" -le 30 ] || fail 'PackageManager unavailable for 30 discovery attempts; no new writes attempted'
      echo 'WAITING: PackageManager unavailable; retaining activation preference'
      sleep 2; continue
    fi
    [ "$result" = 0 ] || fail 'Target discovery failed'
    if [ "$(getprop sys.boot_completed)" != 1 ]; then sleep 2; continue; fi
    transient=0
    now=$(cut -d . -f 1 /proc/uptime)
    for record in "$STATE/worker/"*; do
      [ -f "$record" ] || continue
      pid=${record##*/}
      read -r started ready checked < "$record"
      if ! grep -q "^$pid $started " "$STATE/auto-targets"; then
        if [ "$(getprop sys.boot_completed)" != 1 ]; then sleep 2; continue 2; fi
        # A verified process disappearing from discovery is not sufficient:
        # require that its exact PID/start identity really no longer exists.
        current=$(sed 's/.*) //' "/proc/$pid/stat" 2>/dev/null | awk '{print $20}')
        [ "$current" != "$started" ] || fail 'Patched process still alive but no longer eligible; inspect before retry'
        [ "$planned" = 1 ] || [ $((now-ready)) -ge 30 ] || fail 'App exited during health check; auto disabled'
        rm -f "$record"
      fi
    done
    any_record=0
    for record in "$STATE/worker/"*; do [ ! -f "$record" ] || any_record=1; done
    [ "$any_record" = 1 ] || rm -f "$STATE/restore-needed"
    rm -f "$STATE/soft-restart"
    while read -r pid started package; do
      record=$STATE/worker/$pid
      if [ ! -f "$record" ]; then
        : > "$MODDIR/auto.pending"
        sync
        if sh "$MODDIR/action.sh" _auto-on "$pid:$started"; then :; else
          result=$?
          if [ "$result" = 3 ]; then transient=1; continue; fi
          if [ "$result" = 2 ]; then continue; fi
          exit "$result"
        fi
        printf '%s %s %s\n' "$started" "$now" "$now" > "$record"
      else
        read -r saved ready checked < "$record"
        [ "$saved" = "$started" ] || exit 1
        if [ $((now-checked)) -ge 30 ]; then
          # Serialize health reads with user status/patch operations.
          if ! mkdir "$STATE/lock" 2>/dev/null; then continue; fi
          result=0
          "$MODDIR/bin/gxr-openxr-velocity" "$pid" status > "$STATE/auto-status.log" || result=$?
          rmdir "$STATE/lock"
          # A read-only check can race the framework stop after discovery.
          if [ "$(getprop sys.boot_completed)" != 1 ]; then sleep 2; continue 2; fi
          if [ "$result" = 3 ]; then transient=1; continue; fi
          [ "$result" = 0 ] || exit "$result"
          grep -q '^CORRECTED pid=' "$STATE/auto-status.log"
          printf '%s %s %s\n' "$started" "$ready" "$now" > "$record"
          echo "Verified CORRECTED $package pid=$pid"
        fi
      fi
    done < "$STATE/auto-targets"
    healthy=1
    for record in "$STATE/worker/"*; do
      [ -f "$record" ] || continue
      read -r started ready checked < "$record"
      [ "$checked" -gt "$ready" ] || healthy=0
    done
    [ "$healthy" = 0 ] || rm -f "$MODDIR/auto.pending"
    if [ "$transient" = 1 ]; then
      unavailable=$((unavailable+1))
      [ "$unavailable" -le 30 ] || fail 'PackageManager unavailable for 30 operation attempts'
    else
      unavailable=0
    fi
    sleep 2
  done
  ;;
 status)
  if [ -f "$MODDIR/disable" ]; then echo 'Auto disabled'; else echo 'Auto enabled'; fi
  if [ -d "$STATE/worker" ] && [ -f "$STATE/run" ]; then echo 'Worker marker present'; else echo 'Worker not running'; fi
  ;;
 *) exit 1;;
esac
