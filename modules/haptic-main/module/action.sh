#!/system/bin/sh
set -eu
MODDIR=${0%/*}
if [ "$#" = 0 ]; then
  echo 'Automatic activation waits for sys.boot_completed=1, then up to 20 seconds for the controller service and JNI library. It applies immediately when ready.'
  . "$MODDIR/action-report.sh"
fi
STATE=/dev/gxr_haptic_main
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
if [ "$MODE" = on ] && [ -f "$MODDIR/auto.sh" ]; then exec sh "$MODDIR/auto.sh" enable; fi
if [ "$MODE" = off ] || [ "$MODE" = toggle ]; then
  [ ! -f "$MODDIR/auto.sh" ] || sh "$MODDIR/auto.sh" disable
fi
[ "$MODE" != _auto-off ] || MODE=off
[ "$MODE" != off ] || : > "$MODDIR/disable"
if [ ! -f "$MODDIR/auto.sh" ] && { [ "$MODE" = on ] || [ "$MODE" = toggle ]; }; then rm -f "$MODDIR/disable"; fi
mkdir "$STATE/lock" 2>/dev/null || { echo 'Another action is active'; exit 2; }
trap 'code=$?; rmdir "$STATE/lock"; if [ "$code" -ne 0 ] && [ "$code" -ne 3 ] && [ "$MODE" != status ]; then : > "$MODDIR/disable"; fi; exit "$code"' EXIT
AUTO_ID=
if [ "$MODE" = _auto-on ]; then
  [ ! -f "$MODDIR/disable" ] && [ -f "$STATE/auto.enabled" ] && [ -d "$STATE/auto.worker" ] || fail 'Autostart stopped'
  AUTO_ID=${2:-}
  [ -n "$AUTO_ID" ] || fail 'Missing auto process identity'
  [ "$(getprop sys.boot_completed)" = 1 ] || { echo 'WAITING: Android is restarting; no writes'; exit 3; }
  MODE=on
elif [ "$MODE" != status ]; then
  rm -f "$STATE/auto.enabled"
fi
if [ "$MODE" != off ] && [ "$MODE" != status ]; then
  [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || fail 'Module disabled or marked for removal'
fi
(cd "$MODDIR" && sha256sum -c SHA256SUMS) > "$STATE/checksums.log" || fail 'Module checksum failure'
APK=/product/priv-app/secxrcontroller/secxrcontroller.apk
JNI=/system/lib64/libsecxrcontrollerservice_jni.xrcontroller.samsung.so
[ "$(sha256sum "$APK" | cut -d ' ' -f 1)" = 8130b5542c36235eb1e951b9245684299d6a7ddd1a8c95f231e8241e477673cb ] || fail 'Unsupported controller APK'
[ "$(sha256sum "$JNI" | cut -d ' ' -f 1)" = 80d45131558b28107cf7e3c9a2c483bce4e2207b934e4f25e951f0fc79d1cd36 ] || fail 'Unsupported JNI library'
TARGET=$(pidof com.sec.android.secxrcontrollerservice || true)
if [ -n "$AUTO_ID" ] && [ "$(getprop sys.boot_completed)" != 1 ]; then
  echo 'WAITING: Android is restarting; no writes'; exit 3
fi
if [ -n "$AUTO_ID" ] && [ -z "$TARGET" ]; then echo 'WAITING: controller service disappeared before writes'; exit 3; fi
if [ -z "$TARGET" ] && { [ "$MODE" = status ] || [ "$MODE" = off ]; }; then echo 'WAITING: target not running; no live patch'; exit 0; fi
case "$TARGET" in ''|*[!0-9]*) fail 'Expected exactly one running controller service';; esac
if [ -n "$AUTO_ID" ]; then
  STARTED=$(sed 's/.*) //' "/proc/$TARGET/stat" 2>/dev/null | awk '{print $20}')
  [ "$TARGET:$STARTED" = "$AUTO_ID" ] || { echo 'WAITING: controller service changed before writes'; exit 3; }
  grep -Fq "$JNI" "/proc/$TARGET/maps" 2>/dev/null || { echo 'WAITING: controller JNI library not loaded; no writes'; exit 3; }
fi
if "$MODDIR/bin/gxr-main" "$TARGET" "$MODE" > "$STATE/log" 2>&1; then
  cat "$STATE/log"
  if grep -q "^STOCK " "$STATE/log" && [ "$MODE" != status ]; then : > "$MODDIR/disable"; fi
else
  cat "$STATE/log"
  fail 'Switch failed; no policy changes or service restart attempted.'
fi
