#!/system/bin/sh
umask 077
MODDIR=${0%/*}
BIN="$MODDIR/bin/gxr-quality"
RUN=/dev/gxr-quality-session
case "${1:-status}" in
 resume)
  [ "$MODDIR" = /data/adb/modules/gxr_openxr_quality ] || exit 1
  # The binary resets previous-cold-boot guards without changing preference.
  # No permission, property or original-file changes are made by startup.
  for path in "$RUN" "$RUN/runtime.log" "$MODDIR/SHA256SUMS"; do
   [ ! -L "$path" ] || { touch "$MODDIR/disable"; echo 'DISABLED - Unsafe symlink'; exit 1; }
  done
  mkdir -p "$RUN" || exit 1
  chmod 700 "$RUN"
  if ! (cd "$MODDIR" && sha256sum -c SHA256SUMS) > "$RUN/integrity.log" 2>&1; then
   touch "$MODDIR/disable"
   echo 'DISABLED - Package integrity error; inspect /dev/gxr-quality-session/integrity.log'
   exit 1
  fi
  nohup "$BIN" run-module >> "$RUN/runtime.log" 2>&1 < /dev/null &
  ;;
 status)
  current=$("$BIN" status 2>&1)
  result=$?
  printf '%s\n' "$current"
  [ "$result" -eq 0 ] || exit "$result"
  if [ "$current" = 'DISABLED - No controller or owned override' ]; then
   echo 'GXR_UPDATE_STATE=STOCK'
  fi
  ;;
 off)
  touch "$MODDIR/disable" || exit 1
  if ! "$BIN" stop; then "$BIN" restore || exit 1; fi
  count=0
  while [ "$count" -lt 20 ]; do
   current=$("$BIN" status 2>&1)
   result=$?
   if [ "$result" -eq 0 ] && [ "$current" = 'DISABLED - No controller or owned override' ]; then
    printf '%s\n' "$current"
    echo 'GXR_UPDATE_STATE=STOCK'
    exit 0
   fi
   count=$((count+1))
   sleep 1
  done
  printf '%s\n' "$current"
  echo 'DISABLED - Error: restoration not verified; leave OFF and fully remove power/restart.'
  exit 1
  ;;
 *) echo 'DISABLED - Expected resume, status or off'; exit 1 ;;
esac
