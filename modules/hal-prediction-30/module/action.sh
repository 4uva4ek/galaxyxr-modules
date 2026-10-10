#!/system/bin/sh
set -eu
MODDIR=${0%/*}
[ "$#" != 0 ] || { . "$MODDIR/action-report.sh"; exit; }
. "$MODDIR/common.sh"
MODE=$1
case "$MODE" in
  on) exec sh "$MODDIR/live-control.sh" enable;;
  off) exec sh "$MODDIR/live-control.sh" disable;;
  status|_apply|_restore) ;;
  *) echo 'ERROR: expected on, off or status'; exit 1;;
esac
mkdir "$STATE/lock" 2>/dev/null || { echo 'WAITING: another operation is in progress'; exit 2; }
trap 'rmdir "$STATE/lock"' EXIT
checksums || { [ "$MODE" = status ] && { echo 'ERROR: module checksum mismatch'; exit 1; }; fail 'Module checksum mismatch'; }
identity || { [ "$MODE" = status ] && exit 1; fail 'Cannot identify controller service'; }
if [ -z "$IDENTITY" ]; then
  [ "$MODE" != _apply ] || fail 'Controller service disappeared before activation'
  echo 'DISABLED - Waiting: controller service is not running; no live patch'
  exit 0
fi
NATIVE_MODE=status
if [ "$MODE" = _apply ]; then
  [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || fail 'Module is disabled or marked for removal'
  [ "$(getprop sys.boot_completed)" = 1 ] || fail 'Android boot has not completed'
  [ "${2:-}" = "$IDENTITY" ] || fail 'Controller service identity changed before activation'
  NATIVE_MODE=on
fi
[ "$MODE" != _restore ] || NATIVE_MODE=off
code=0
"$MODDIR/bin/gxr-hal-lead30" "$TARGET" "$NATIVE_MODE" > "$STATE/native.log" 2>&1 || code=$?
sed -E 's/^HAL_PLUS_30MS([ :])/ENABLED\1/; s/^STOCK([ :])/DISABLED\1/; s/; no persistent activation/; helper modifies private memory only/' "$STATE/native.log"
if [ "$code" != 0 ]; then
  [ "$MODE" != status ] || exit "$code"
  fail 'Native operation failed. Disable the module, then reboot if restoration is not confirmed.'
fi
