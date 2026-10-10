#!/system/bin/sh
set -eu
MODDIR=${0%/*}
STATE=/dev/gxr_haptic_main
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
APK=/product/priv-app/secxrcontroller/secxrcontroller.apk
JNI=/system/lib64/libsecxrcontrollerservice_jni.xrcontroller.samsung.so
[ "$(sha256sum "$APK" | cut -d ' ' -f 1)" = 8130b5542c36235eb1e951b9245684299d6a7ddd1a8c95f231e8241e477673cb ] || fail 'Unsupported controller APK'
[ "$(sha256sum "$JNI" | cut -d ' ' -f 1)" = 80d45131558b28107cf7e3c9a2c483bce4e2207b934e4f25e951f0fc79d1cd36 ] || fail 'Unsupported JNI library'
TARGET=$(pidof com.sec.android.secxrcontrollerservice || true)
case "$TARGET" in ''|*[!0-9]*) fail 'Expected exactly one running controller service';; esac
if [ -n "$AUTO_ID" ]; then
  STARTED=$(sed 's/.*) //' "/proc/$TARGET/stat" | awk '{print $20}')
  [ "$TARGET:$STARTED" = "$AUTO_ID" ] || fail 'Target changed before automatic activation'
fi
if "$MODDIR/bin/gxr-main" "$TARGET" "$MODE" > "$STATE/log" 2>&1; then
  cat "$STATE/log"
  echo 'Action toggles MAIN/STOCK. Disable auto before reboot for rollback.'
else
  cat "$STATE/log"
  fail 'Switch failed; no policy changes or service restart attempted.'
fi
