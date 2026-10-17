#!/system/bin/sh
set -u
MODDIR=$(CDPATH= cd -P "${0%/*}" && pwd) || exit 1
STATE=/dev/gxr_stream_cpu_relief
BIN=$MODDIR/bin/gxr-stream-cpu
fail() { : > "$MODDIR/disable"; echo "DISABLED - Error: $*"; exit 1; }
case "${1:-status}" in
 status) exec "$BIN" "$MODDIR" status ;;
 disable)
  : > "$MODDIR/disable"
  count=0
  while :; do
   "$BIN" "$MODDIR" restore
   result=$?
   [ "$result" = 2 ] || exit "$result"
   count=$((count+1)); [ "$count" -lt 15 ] || fail 'Worker did not restore in time. Leave disabled and fully power-cycle.'
   sleep 1
  done ;;
 resume)
  sh "$MODDIR/cold-boot-reset.sh" "$MODDIR" "$STATE" || fail 'Cold boot guard check failed'
  [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || exit 0
  nohup sh "$MODDIR/auto.sh" _run >> "$STATE/worker.log" 2>&1 < /dev/null &
  ;;
 _run)
  count=0
  while [ "$(getprop sys.boot_completed)" != 1 ]; do
   [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || exit 0
   count=$((count+1)); [ "$count" -lt 150 ] || fail 'Android boot timeout'
   sleep 2
  done
  (cd "$MODDIR" && sha256sum -c SHA256SUMS) || fail 'Package checksum mismatch'
  "$BIN" "$MODDIR" run
  result=$?
  [ "$result" = 0 ] || [ "$result" = 2 ] || fail 'Worker failed; see worker.log and Action status'
  ;;
 *) fail 'Expected resume, disable or status' ;;
esac
