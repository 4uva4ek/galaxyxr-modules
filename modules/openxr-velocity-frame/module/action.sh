#!/system/bin/sh
set -eu
MODDIR=${0%/*}
if [ "$#" = 0 ]; then
  echo 'App exits and crashes do not disable this module. The worker waits for a new app process.'
  echo 'Ordinary app restarts do not require a cold boot. Interrupted writes and verification errors still require reviewed recovery.'
  . "$MODDIR/action-report.sh"
fi
STATE=/dev/gxr_openxr_velocity_frame
fail() { [ "${1:-}" = "" ] || echo "$*"; [ "${MODE:-status}" = status ] || : > "$MODDIR/disable"; exit 1; }
[ "$(id -u)" = 0 ] || fail 'Root required'
awk '$2 == "/dev" && $3 == "tmpfs" {ok=1} END {exit !ok}' /proc/mounts || fail '/dev must be tmpfs'
[ ! -L "$STATE" ] || fail 'Unsafe state path'
umask 077
mkdir -p "$STATE"
DEFAULT=toggle
[ ! -f "$MODDIR/auto.sh" ] || DEFAULT=status
MODE=${1:-$DEFAULT}
case "$MODE" in on|off|status|toggle|_auto-on|_auto-off) ;; *) fail 'Expected on, off, status or toggle';; esac
if [ -f "$MODDIR/auto.sh" ]; then
  if [ "$MODE" = toggle ]; then
    if [ -f "$MODDIR/disable" ]; then MODE=on; else MODE=off; fi
  fi
  [ "$MODE" != on ] || exec sh "$MODDIR/auto.sh" enable
elif [ "$MODE" = on ] || [ "$MODE" = toggle ]; then
  rm -f "$MODDIR/disable"
fi
if [ "$MODE" = off ] || [ "$MODE" = toggle ]; then
  [ ! -f "$MODDIR/auto.sh" ] || sh "$MODDIR/auto.sh" disable
fi
# Both modules modify consumePose; serialize their native operations.
GLOBAL_LOCK=/dev/gxr_openxr_transaction
[ ! -L "$GLOBAL_LOCK" ] || fail 'Unsafe shared lock path'
mkdir "$GLOBAL_LOCK" 2>/dev/null || { echo 'WAITING: another OpenXR module operation is active'; exit 2; }
mkdir "$STATE/lock" 2>/dev/null || { rmdir "$GLOBAL_LOCK"; echo 'Another action is active'; exit 2; }
trap 'code=$?; rmdir "$STATE/lock"; rmdir "$GLOBAL_LOCK"; if [ "$code" -ne 0 ] && [ "$code" -ne 3 ] && [ "$code" -ne 4 ] && [ "$MODE" != status ]; then : > "$MODDIR/disable"; fi; exit "$code"' EXIT
(cd "$MODDIR" && sha256sum -c SHA256SUMS) > "$STATE/checksums.log" || fail 'Module checksum failure'
if [ "$MODE" = _auto-on ] && [ "$(getprop sys.boot_completed)" != 1 ]; then
  echo 'WAITING: Android is restarting; no writes'; exit 3
fi
result=0
"$MODDIR/bin/gxr-openxr-velocity" list > "$STATE/targets" || result=$?
if [ "$result" != 0 ]; then
  if [ "$result" = 3 ] && { [ "$MODE" = _auto-on ] || [ "$MODE" = status ]; }; then exit 3; fi
  fail 'Cannot discover apps for this operation; no verified restoration'
fi
FILTER=
if [ "$MODE" = _auto-on ]; then
  [ -f "$STATE/run" ] && [ -d "$STATE/worker" ] || fail 'Worker stopped'
  FILTER=${2:-}; [ -n "$FILTER" ] || fail 'Missing identity'
  MODE=on
fi
[ "$MODE" != _auto-off ] || MODE=off
[ "$MODE" != off ] || : > "$MODDIR/disable"
if [ "$MODE" = on ]; then
  [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || fail 'Module disabled'
fi
count=0
failed=0
while read -r TARGET STARTED PACKAGE; do
  [ -z "$FILTER" ] || [ "$TARGET:$STARTED" = "$FILTER" ] || continue
  result=0
  "$MODDIR/bin/gxr-openxr-velocity" "$TARGET" identity "$STARTED" || result=$?
  if [ "$result" = 4 ]; then continue; fi
  [ "$result" = 0 ] || fail 'Cannot verify target identity; no writes'
  OP=$MODE
  if [ "$OP" = toggle ]; then
    "$MODDIR/bin/gxr-openxr-velocity" "$TARGET" status > "$STATE/log"
    if grep -q '^CORRECTED ' "$STATE/log"; then OP=off; else OP=on; fi
  fi
  [ "$OP" != off ] || : > "$MODDIR/disable"
  if [ "$OP" = on ]; then
    [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || fail 'Module disabled'
    [ "$(getprop sys.boot_completed)" = 1 ] || { echo 'WAITING: Android is restarting; no writes'; exit 3; }
  fi
  echo "APP $PACKAGE pid=$TARGET"
  NEW_RESTORE=0
  if [ "$OP" = on ] && [ ! -f "$STATE/restore-needed" ]; then
    : > "$STATE/restore-needed"; NEW_RESTORE=1
  fi
  if "$MODDIR/bin/gxr-openxr-velocity" "$TARGET" "$OP" "$STARTED" > "$STATE/log" 2>&1; then cat "$STATE/log"; else
    result=$?
    cat "$STATE/log"
    # The native helper reserves 3 for failed package discovery, before it
    # opens target memory. Automatic activation addresses exactly one app.
    if { [ "$result" = 3 ] || [ "$result" = 4 ]; } && [ "$OP" = on ] && [ -n "$FILTER" ]; then
      [ "$NEW_RESTORE" = 0 ] || rm -f "$STATE/restore-needed"
      exit "$result"
    fi
    # The helper proves this exact process exited before any write. There is
    # no private patch left to restore in a dead process; continue other apps.
    [ "$result" != 4 ] || continue
    # Restore every eligible app even if one mapping no longer validates.
    [ "$OP" = off ] || exit 1
    failed=1
  fi
  count=$((count+1))
done < "$STATE/targets"
if [ -n "$FILTER" ] && [ "$count" != 1 ]; then
  result=0
  "$MODDIR/bin/gxr-openxr-velocity" "${FILTER%:*}" identity "${FILTER#*:}" || result=$?
  [ "$result" != 4 ] || { echo 'WAITING: target exited before activation; no writes'; exit 4; }
  fail 'Target still alive but unavailable for activation; no writes'
fi
[ "$count" != 0 ] || echo 'WAITING: no third-party main app process has OpenXR loaded'
exit "$failed"
