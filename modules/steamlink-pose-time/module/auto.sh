#!/system/bin/sh
set -eu
MODDIR=${0%/*}
STATE=/dev/gxr_steamlink_pose_time
[ "$(id -u)" = 0 ] || exit 1
awk '$2=="/dev" && $3=="tmpfs" {ok=1} END {exit !ok}' /proc/mounts
[ ! -L "$STATE" ] || exit 1
umask 077
mkdir -p "$STATE"
fail() { echo "DISABLED - Error: $*"; printf '%s\n' "$*" > "$STATE/failed"; : > "$MODDIR/disable"; exit 1; }
case "${1:-status}" in
 resume)
  sh "$MODDIR/cold-boot-reset.sh" "$MODDIR" "$STATE" || fail 'Cold-boot guard check failed.'
  [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || exit 0
  [ ! -f "$MODDIR/auto.pending" ] && [ ! -f "$STATE/failed" ] || fail 'Previous activation failed or was interrupted; inspect Action.'
  mkdir "$STATE/worker" 2>/dev/null || { echo 'DISABLED - Waiting: worker already starting/running.'; exit 0; }
  nohup sh "$MODDIR/auto.sh" _run > "$STATE/auto.log" 2>&1 < /dev/null &
  ;;
 _run)
  cleanup() {
   result=$?
   trap - EXIT
   if ! "$MODDIR/bin/gxr-steamlink-pose-time" restore; then
    echo 'DISABLED - Error: restore failed; leave disabled and restart Steam Link.'
    : > "$MODDIR/disable"; result=1
   fi
   rmdir "$STATE/worker" 2>/dev/null || true
   exit "$result"
  }
  trap cleanup EXIT
  while [ "$(getprop sys.boot_completed)" != 1 ]; do
   [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || exit 0
   sleep 2
  done
  [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || exit 0
  (cd "$MODDIR" && sha256sum -c SHA256SUMS) > "$STATE/checksums.log" || fail 'Package checksum failure.'
  "$MODDIR/bin/gxr-steamlink-pose-time" worker || fail 'Worker failed; inspect auto.log.'
  ;;
 disable)
  : > "$MODDIR/disable"
  count=0
  while [ -d "$STATE/worker" ]; do
   count=$((count+1))
   if [ "$count" -gt 12 ]; then
    "$MODDIR/bin/gxr-steamlink-pose-time" restore || fail 'Restoration failed; restart Steam Link with module disabled.'
    fail 'Hooks restored but worker shutdown is unconfirmed; leave disabled.'
   fi
   sleep 1
  done
  "$MODDIR/bin/gxr-steamlink-pose-time" restore
  ;;
 *) echo 'Expected resume or disable'; exit 1;;
esac
