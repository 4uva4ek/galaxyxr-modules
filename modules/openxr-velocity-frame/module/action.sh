#!/system/bin/sh
set -eu
MODDIR=${0%/*}
if [ "$#" = 0 ]; then . "$MODDIR/action-report.sh"; fi
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
  [ "$MODE" != on ] || exec sh "$MODDIR/live-control.sh" enable
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
trap 'code=$?; rmdir "$STATE/lock"; rmdir "$GLOBAL_LOCK"; if [ "$code" -ne 0 ] && [ "$MODE" != status ]; then : > "$MODDIR/disable"; fi; exit "$code"' EXIT
(cd "$MODDIR" && sha256sum -c SHA256SUMS) > "$STATE/checksums.log" || fail 'Module checksum failure'
"$MODDIR/bin/gxr-openxr-velocity" list > "$STATE/targets"
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
  CURRENT=$(sed 's/.*) //' "/proc/$TARGET/stat" | awk '{print $20}')
  [ "$CURRENT" = "$STARTED" ] || fail 'Target identity changed'
  OP=$MODE
  if [ "$OP" = toggle ]; then
    "$MODDIR/bin/gxr-openxr-velocity" "$TARGET" status > "$STATE/log"
    if grep -q '^CORRECTED ' "$STATE/log"; then OP=off; else OP=on; fi
  fi
  [ "$OP" != off ] || : > "$MODDIR/disable"
  if [ "$OP" = on ]; then
    [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || fail 'Module disabled'
  fi
  echo "APP $PACKAGE pid=$TARGET"
  if "$MODDIR/bin/gxr-openxr-velocity" "$TARGET" "$OP" > "$STATE/log" 2>&1; then cat "$STATE/log"; else
    cat "$STATE/log"
    # Restore every eligible app even if one mapping no longer validates.
    [ "$OP" = off ] || exit 1
    failed=1
  fi
  count=$((count+1))
done < "$STATE/targets"
[ -z "$FILTER" ] || [ "$count" = 1 ] || fail 'Target disappeared before activation'
[ "$count" != 0 ] || echo 'WAITING: no third-party main app process has OpenXR loaded'
exit "$failed"
