#!/system/bin/sh
# KernelSU live-toggle protocol v1. No independent activation preference.
set -eu
MODDIR=${0%/*}
fail() { : > "$MODDIR/disable"; echo "$*"; exit 1; }
[ "$(id -u)" = 0 ] || exit 1
case "${1:-status}" in
 enable)
  [ ! -f "$MODDIR/remove" ] || fail 'Module marked for removal'
  rm -f "$MODDIR/disable"
  if [ -f "$MODDIR/auto.sh" ]; then
    sh "$MODDIR/auto.sh" enable || fail 'Automatic activation failed'
    # A checked app module can wait for the app, but must not silently remain STOCK.
    count=0
    while [ "$count" -lt 12 ]; do
      [ ! -f "$MODDIR/disable" ] || fail 'Activation disabled by worker'
      if result=$(sh "$MODDIR/action.sh" status 2>&1); then
        if printf '%s\n' "$result" | grep -Eq '^(MAIN|EXTRAPOLATION|CORRECTED|PREDICTED_40MS|WAITING|ОЖИДАНИЕ)'; then
          if ! printf '%s\n' "$result" | grep -q '^STOCK '; then printf '%s\n' "$result"; exit 0; fi
        fi
      fi
      count=$((count+1)); sleep 1
    done
    sh "$MODDIR/auto.sh" disable || true
    fail 'Activation was not confirmed; module disabled'
  else
    sh "$MODDIR/action.sh" on || fail 'Manual activation failed'
  fi
  ;;
 disable)
  : > "$MODDIR/disable"
  sh "$MODDIR/action.sh" off || fail 'Disabled, but restore failed; see Action log'
  ;;
 status) exec sh "$MODDIR/action.sh" status ;;
 *) fail 'Expected enable, disable or status';;
esac
