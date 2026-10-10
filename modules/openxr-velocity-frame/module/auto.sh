#!/system/bin/sh
set -eu
MODDIR=${0%/*}
STATE=/dev/gxr_openxr_velocity_frame
[ "$(id -u)" = 0 ] || exit 1
awk '$2 == "/dev" && $3 == "tmpfs" {ok=1} END {exit !ok}' /proc/mounts
[ ! -L "$STATE" ] || exit 1
umask 077
mkdir -p "$STATE"
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
    sh "$MODDIR/action.sh" _auto-off >> "$STATE/auto.log" 2>&1 || echo 'Restore failed; close patched apps to clear private patches'
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
  while [ -f "$STATE/run" ]; do
    [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || exit 0
    "$MODDIR/bin/gxr-openxr-velocity" list > "$STATE/auto-targets"
    now=$(cut -d . -f 1 /proc/uptime)
    for record in "$STATE/worker/"*; do
      [ -f "$record" ] || continue
      pid=${record##*/}
      read -r started ready checked < "$record"
      if ! grep -q "^$pid $started " "$STATE/auto-targets"; then
        [ $((now-ready)) -ge 30 ] || { echo 'App exited during health check; auto disabled'; exit 1; }
        rm -f "$record"
      fi
    done
    while read -r pid started package; do
      record=$STATE/worker/$pid
      if [ ! -f "$record" ]; then
        : > "$MODDIR/auto.pending"
        sync
        if sh "$MODDIR/action.sh" _auto-on "$pid:$started"; then :; else
          result=$?
          [ "$result" = 2 ] && continue
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
    sleep 2
  done
  ;;
 status)
  if [ -f "$MODDIR/disable" ]; then echo 'Auto disabled'; else echo 'Auto enabled'; fi
  if [ -d "$STATE/worker" ] && [ -f "$STATE/run" ]; then echo 'Worker marker present'; else echo 'Worker not running'; fi
  ;;
 *) exit 1;;
esac
