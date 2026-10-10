#!/system/bin/sh
set -eu
MODDIR=${0%/*}
if [ "$#" = 0 ]; then . "$MODDIR/action-report.sh"; fi
STATE=/dev/gxr_controller_extrapolation
fail() { [ "${1:-}" = "" ] || echo "$*"; [ "${MODE:-status}" = status ] || : > "$MODDIR/disable"; exit 1; }
[ "$(id -u)" = 0 ] || fail 'Root required'
DEFAULT=toggle
[ ! -f "$MODDIR/auto.sh" ] || DEFAULT=status
MODE=${1:-$DEFAULT}
case "$MODE" in toggle|on|off|status|_auto-on|_auto-off) ;; *) fail 'Expected toggle, on, off or status';; esac
awk '$2 == "/dev" && $3 == "tmpfs" { ok=1 } END { exit !ok }' /proc/mounts || fail '/dev must be tmpfs'
[ ! -L "$STATE" ] || fail 'Unsafe state path'
umask 077
mkdir -p "$STATE"
if [ "$MODE" = toggle ] && [ -f "$MODDIR/auto.sh" ]; then
  if [ -f "$MODDIR/disable" ]; then MODE=on; else MODE=off; fi
fi
if [ "$MODE" = on ] && [ -f "$MODDIR/auto.sh" ]; then exec sh "$MODDIR/live-control.sh" enable; fi
STOP_FAILED=0
if [ "$MODE" = off ] || [ "$MODE" = toggle ]; then
  if [ -f "$MODDIR/auto.sh" ] && ! sh "$MODDIR/auto.sh" disable; then
    STOP_FAILED=1
    echo 'ERROR: worker stop was not confirmed; attempting guarded live restoration anyway.'
  fi
fi
[ "$MODE" != _auto-off ] || MODE=off
[ "$MODE" != off ] || : > "$MODDIR/disable"
if [ ! -f "$MODDIR/auto.sh" ] && { [ "$MODE" = on ] || [ "$MODE" = toggle ]; }; then rm -f "$MODDIR/disable"; fi
# Both modules modify consumePose; serialize their native operations.
GLOBAL_LOCK=/dev/gxr_openxr_transaction
[ ! -L "$GLOBAL_LOCK" ] || fail 'Unsafe shared lock path'
mkdir "$GLOBAL_LOCK" 2>/dev/null || { echo 'WAITING: another OpenXR module operation is active'; exit 2; }
mkdir "$STATE/lock" 2>/dev/null || { rmdir "$GLOBAL_LOCK"; echo 'Another action is active'; exit 2; }
trap 'code=$?; rmdir "$STATE/lock"; rmdir "$GLOBAL_LOCK"; if [ "$code" -ne 0 ] && [ "$MODE" != status ]; then : > "$MODDIR/disable"; fi; exit "$code"' EXIT
AUTO_ID=
if [ "$MODE" = _auto-on ]; then
  [ ! -f "$MODDIR/disable" ] && [ -f "$STATE/auto.enabled" ] && [ -d "$STATE/auto.worker" ] || fail 'Autostart stopped'
  AUTO_ID=${2:-}
  [ -n "$AUTO_ID" ] || fail 'Missing auto process identity'
  MODE=on
elif [ "$MODE" != status ]; then
  rm -f "$STATE/auto.enabled"
fi
if [ "$MODE" != off ] && [ "$MODE" != status ]; then
  [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || fail 'Module disabled or marked for removal'
fi
(cd "$MODDIR" && sha256sum -c SHA256SUMS) > "$STATE/checksums.log" || fail 'Module checksum failure'
RUNTIME=/system/lib64/libopenxr_android.so
[ "$(sha256sum "$RUNTIME" | cut -d ' ' -f 1)" = a7dabaa1030cc7979efe987f212488c61b61e8afb9aa3add59f7ae4fbeff4c92 ] || fail 'Unsupported OpenXR runtime'
TARGET=$(pidof com.valvesoftware.steamlinkvr || true)
if [ -z "$TARGET" ] && { [ "$MODE" = status ] || [ "$MODE" = off ]; }; then echo 'WAITING: target not running; no live patch'; exit 0; fi
case "$TARGET" in ''|*[!0-9]*) fail 'Expected exactly one running Steam Link process';; esac
if [ -n "$AUTO_ID" ]; then
  STARTED=$(sed 's/.*) //' "/proc/$TARGET/stat" | awk '{print $20}')
  [ "$TARGET:$STARTED" = "$AUTO_ID" ] || fail 'Target changed before automatic activation'
fi
if "$MODDIR/bin/gxr-extrapolation" "$TARGET" "$MODE" > "$STATE/log" 2>&1; then
  cat "$STATE/log"
  if grep -q "^STOCK " "$STATE/log" && [ "$MODE" != status ]; then : > "$MODDIR/disable"; fi
else
  cat "$STATE/log"
  fail 'Switch failed; no policy changes or service restart attempted.'
fi
[ "$STOP_FAILED" = 0 ] || fail 'Live restoration completed, but worker exit is unconfirmed. Leave disabled; restart the app. Reboot if the worker remains stuck.'
