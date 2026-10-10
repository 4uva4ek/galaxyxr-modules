#!/system/bin/sh
set -eu
MODDIR=${0%/*}
STATE=/dev/gxr_controller_extrapolation
fail() { echo "$*"; exit 1; }
[ "$(id -u)" = 0 ] || fail 'Root required'
MODE=${1:-toggle}
case "$MODE" in toggle|on|off|status|_auto-on) ;; *) fail 'Expected toggle, on, off or status';; esac
awk '$2 == "/dev" && $3 == "tmpfs" { ok=1 } END { exit !ok }' /proc/mounts || fail '/dev must be tmpfs'
[ ! -L "$STATE" ] || fail 'Unsafe state path'
umask 077
mkdir -p "$STATE"
case "$MODE" in
  on|off|toggle) if [ -f "$MODDIR/auto.sh" ]; then sh "$MODDIR/auto.sh" disable; fi ;;
esac
mkdir "$STATE/lock" 2>/dev/null || fail 'Another action is active; reboot clears a stale lock'
trap 'rmdir "$STATE/lock"' EXIT
AUTO_ID=
if [ "$MODE" = _auto-on ]; then
  [ -f "$MODDIR/autostart/enabled" ] && [ -f "$STATE/auto.enabled" ] && [ -d "$STATE/auto.worker" ] || fail 'Autostart stopped'
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
case "$TARGET" in ''|*[!0-9]*) fail 'Expected exactly one running Steam Link process';; esac
if [ -n "$AUTO_ID" ]; then
  STARTED=$(sed 's/.*) //' "/proc/$TARGET/stat" | awk '{print $20}')
  [ "$TARGET:$STARTED" = "$AUTO_ID" ] || fail 'Target changed before automatic activation'
fi
if "$MODDIR/bin/gxr-extrapolation" "$TARGET" "$MODE" > "$STATE/log" 2>&1; then
  cat "$STATE/log"
  echo 'Action toggles EXTRAPOLATION/STOCK. Disable autostart before reboot to restore STOCK. App exit clears this patch; autostart may reapply it.'
else
  cat "$STATE/log"
  fail 'Switch failed; no policy changes or service restart attempted.'
fi
