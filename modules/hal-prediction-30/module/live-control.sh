#!/system/bin/sh
set -eu
MODDIR=${0%/*}
. "$MODDIR/common.sh"
case "${1:-status}" in
  enable)
    [ ! -f "$MODDIR/remove" ] || fail 'Module marked for removal'
    # A retry always restores first. No second saved activation preference.
    sh "$MODDIR/live-control.sh" disable || fail 'Cannot restore before activation'
    checksums || fail 'Module checksum mismatch'
    rm -f "$MODDIR/last-error" "$MODDIR/activation.pending"
    rm -f "$MODDIR/disable"
    if [ -f "$MODDIR/auto.sh" ]; then
      sh "$MODDIR/auto.sh" resume || fail 'Cannot start automatic worker'
      count=0
      while [ "$count" -lt 12 ]; do
        [ ! -f "$MODDIR/disable" ] || { cat "$MODDIR/last-error" 2>/dev/null || true; exit 1; }
        if result=$(sh "$MODDIR/action.sh" status 2>&1); then
          if printf '%s\n' "$result" | grep -q '^ENABLED '; then printf '%s\n' "$result"; exit 0; fi
        fi
        if [ "$(getprop sys.boot_completed)" != 1 ]; then echo 'DISABLED - Waiting: Android boot must complete'; exit 0; fi
        identity || fail 'Cannot identify controller service'
        if [ -z "$IDENTITY" ]; then echo 'DISABLED - Waiting: controller service must start'; exit 0; fi
        if ! grep -Fq "$LIBRARY" "/proc/$TARGET/maps"; then echo 'DISABLED - Waiting: controller library must load'; exit 0; fi
        count=$((count+1)); sleep 1
      done
      sh "$MODDIR/live-control.sh" disable || true
      fail 'Activation not confirmed within 12 seconds'
    else
      identity || fail 'Cannot identify controller service'
      sh "$MODDIR/action.sh" _apply "$IDENTITY" || fail 'Manual activation failed'
    fi
    ;;
  disable)
    : > "$MODDIR/disable"
    rm -f "$STATE/run"
    count=0
    while [ -d "$STATE/worker" ]; do
      count=$((count+1)); [ "$count" -le 15 ] || fail 'Worker did not stop. Module disabled; reboot to clear private patch.'
      sleep 1
    done
    code=2; count=0
    while [ "$code" = 2 ] && [ "$count" -lt 5 ]; do
      code=0
      sh "$MODDIR/action.sh" _restore || code=$?
      count=$((count+1))
      [ "$code" != 2 ] || sleep 1
    done
    [ "$code" = 0 ] || fail 'Restoration failed. Module disabled; reboot to clear private patch.'
    rm -f "$MODDIR/activation.pending"
    ;;
  status) exec sh "$MODDIR/action.sh" status;;
  *) echo 'ERROR: expected enable, disable or status'; exit 1;;
esac
