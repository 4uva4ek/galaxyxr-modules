#!/system/bin/sh
set -eu
MODDIR=${0%/*}
. "$MODDIR/common.sh"
case "${1:-status}" in
  resume)
    [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || exit 0
    mkdir "$STATE/worker" 2>/dev/null || exit 0
    if [ -f "$MODDIR/activation.pending" ]; then
      rmdir "$STATE/worker"
      fail 'Previous activation or initial health check was interrupted. Automatic retry blocked; inspect before re-enabling.'
    fi
    : > "$STATE/run"
    nohup sh "$MODDIR/auto.sh" _run > "$STATE/auto.log" 2>&1 < /dev/null &
    ;;
  _run)
    [ -d "$STATE/worker" ] || exit 1
    cleanup() {
      code=$?
      trap - EXIT
      rm -f "$STATE/run"
      if [ "$code" != 0 ]; then
        : > "$MODDIR/disable"
        [ -f "$MODDIR/last-error" ] || echo 'Automatic worker failed; inspect auto.log' > "$MODDIR/last-error"
      fi
      restored=2; tries=0
      while [ "$restored" = 2 ] && [ "$tries" -lt 5 ]; do
        restored=0
        sh "$MODDIR/action.sh" _restore || restored=$?
        tries=$((tries+1)); [ "$restored" != 2 ] || sleep 1
      done
      if [ "$restored" != 0 ]; then
        : > "$MODDIR/disable"
        echo 'Restore failed. Module disabled; reboot to clear private patch.' | tee "$MODDIR/last-error"
        code=1
      fi
      rmdir "$STATE/worker"
      exit "$code"
    }
    trap cleanup EXIT
    trap 'exit 1' HUP INT TERM
    checksums || fail 'Module checksum mismatch'
    ticks=0
    while [ "$(getprop sys.boot_completed)" != 1 ]; do
      [ -f "$STATE/run" ] && [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || exit 0
      ticks=$((ticks+1)); [ "$ticks" -le 150 ] || fail 'Android boot did not complete within five minutes'
      sleep 2
    done
    last= candidate= ready=0 checked=0 wait_ticks=0
    while [ -f "$STATE/run" ] && [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ]; do
      identity || fail 'Cannot identify controller service'
      now=$(cut -d . -f 1 /proc/uptime)
      if [ -n "$last" ] && [ "$IDENTITY" != "$last" ]; then
        fail 'Controller service exited or restarted after activation. No automatic reapplication.'
      fi
      if [ -z "$last" ]; then
        wait_ticks=$((wait_ticks+1))
        [ "$wait_ticks" -le 150 ] || fail 'Controller service/library did not become ready within five minutes'
        if [ -n "$IDENTITY" ] && [ "$candidate" = "$IDENTITY" ] && grep -Fq "$LIBRARY" "/proc/$TARGET/maps"; then
          : > "$MODDIR/activation.pending"
          sync
          code=0
          sh "$MODDIR/action.sh" _apply "$IDENTITY" || code=$?
          if [ "$code" = 2 ]; then sleep 2; continue; fi
          [ "$code" = 0 ] || fail 'Guarded HAL activation failed'
          last=$IDENTITY; ready=$now; checked=$now
        fi
        candidate=$IDENTITY
      elif [ $((now-checked)) -ge 30 ]; then
        code=0
        sh "$MODDIR/action.sh" status > "$STATE/health.log" 2>&1 || code=$?
        if [ "$code" = 2 ]; then sleep 2; continue; fi
        [ "$code" = 0 ] && grep -q '^ENABLED ' "$STATE/health.log" || fail 'HAL patch health verification failed'
        rm -f "$MODDIR/activation.pending"
        sync
        checked=$now
        echo "Verified +30 ms code and process identity: $IDENTITY (not tracking quality)"
      fi
      sleep 2
    done
    ;;
  *) echo 'ERROR: expected resume'; exit 1;;
esac
