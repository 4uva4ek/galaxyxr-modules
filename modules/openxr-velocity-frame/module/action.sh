#!/system/bin/sh
set -eu
MODDIR=${0%/*}
STATE=/dev/gxr_openxr_velocity_frame
fail() { echo "$*"; exit 1; }
[ "$(id -u)" = 0 ] || fail 'Root required'
awk '$2 == "/dev" && $3 == "tmpfs" {ok=1} END {exit !ok}' /proc/mounts || fail '/dev must be tmpfs'
[ ! -L "$STATE" ] || fail 'Unsafe state path'
umask 077
mkdir -p "$STATE"
DEFAULT=toggle
[ ! -f "$MODDIR/auto.sh" ] || DEFAULT=status
MODE=${1:-$DEFAULT}
case "$MODE" in on|off|status|toggle|_auto-on|_auto-off) ;; *) fail 'Expected on, off, status or toggle';; esac
if [ "$MODE" = off ] || [ "$MODE" = toggle ]; then
  [ ! -f "$MODDIR/auto.sh" ] || sh "$MODDIR/auto.sh" disable
fi
mkdir "$STATE/lock" 2>/dev/null || fail 'Another action is active'
trap 'rmdir "$STATE/lock"' EXIT
(cd "$MODDIR" && sha256sum -c SHA256SUMS) > "$STATE/checksums.log" || fail 'Module checksum failure'
"$MODDIR/bin/gxr-openxr-velocity" list > "$STATE/targets"
FILTER=
if [ "$MODE" = _auto-on ]; then
  [ -f "$STATE/run" ] && [ -d "$STATE/worker" ] || fail 'Worker stopped'
  FILTER=${2:-}; [ -n "$FILTER" ] || fail 'Missing identity'
  MODE=on
fi
[ "$MODE" != _auto-off ] || MODE=off
if [ "$MODE" = on ]; then
  [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || fail 'Module disabled'
  [ ! -f /data/adb/modules/gxr_controller_extrapolation/autostart/enabled ] || fail 'Disable extrapolation autostart first'
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
  if [ "$OP" = on ]; then
    [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || fail 'Module disabled'
    [ ! -f /data/adb/modules/gxr_controller_extrapolation/autostart/enabled ] || fail 'Disable extrapolation autostart first'
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
