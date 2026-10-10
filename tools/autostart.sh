# Shared KernelSU-checkbox-controlled autostart. Source from the module's auto.sh.
set -eu
CONFIG=$MODDIR/autostart
fail() {
  echo "$*"
  : > "$MODDIR/disable"
  if [ -d "$CONFIG" ] && [ ! -L "$CONFIG" ]; then
    rm -f "$CONFIG/enabled"
    printf '%s\n' "$*" > "$CONFIG/failed"
    sync
  fi
  exit 1
}
[ "$(id -u)" = 0 ] || fail 'Root required'
awk '$2 == "/dev" && $3 == "tmpfs" { ok=1 } END { exit !ok }' /proc/mounts || fail '/dev must be tmpfs'
[ ! -L "$STATE" ] || fail 'Unsafe state path'
umask 077
mkdir -p "$STATE"
[ ! -L "$CONFIG" ] || { echo 'Unsafe config path'; exit 1; }
case "${1:-status}" in
  enable)
    sh "$MODDIR/auto.sh" disable
    mkdir -p "$CONFIG"
    rm -f "$MODDIR/disable" "$CONFIG/pending" "$CONFIG/failed"
    : > "$CONFIG/enabled"
    sync
    exec sh "$MODDIR/auto.sh" resume
    ;;
  resume)
    [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || exit 0
    mkdir -p "$CONFIG"
    # Compatibility mirror only. KernelSU disable is the sole saved preference.
    : > "$CONFIG/enabled"
    # Duplicate KernelSU events must not disturb an already-running worker.
    mkdir "$STATE/auto.worker" 2>/dev/null || exit 0
    if [ -f "$CONFIG/pending" ]; then
      rmdir "$STATE/auto.worker"
      fail 'Previous activation did not complete; autostart disabled'
    fi
    : > "$STATE/auto.enabled"
    nohup sh "$MODDIR/auto.sh" _run > "$STATE/auto.log" 2>&1 < /dev/null &
    echo 'Autostart enabled. KernelSU will resume it after root becomes available.'
    ;;
  disable)
    : > "$MODDIR/disable"
    mkdir -p "$CONFIG"
    rm -f "$CONFIG/enabled"
    sync
    rm -f "$STATE/auto.enabled"
    count=0
    while [ -d "$STATE/auto.worker" ]; do
      count=$((count + 1))
      [ "$count" -le 15 ] || fail 'Worker did not exit; reboot clears it. No process was killed.'
      sleep 1
    done
    echo 'Module disabled in KernelSU; worker stopped.'
    ;;
  status)
    if [ ! -f "$MODDIR/disable" ]; then
      echo 'Autostart enabled persistently (see auto.log for worker progress)'
    else
      echo 'Autostart disabled'
    fi
    [ ! -f "$CONFIG/failed" ] || cat "$CONFIG/failed"
    ;;
  _run)
    [ -d "$STATE/auto.worker" ] || exit 0
    trap 'rm -f "$STATE/auto.enabled"; rmdir "$STATE/auto.worker"' EXIT
    [ -f "$STATE/auto.enabled" ] && [ ! -f "$MODDIR/disable" ] || exit 0
    # Fail closed even on unexpected shell errors or termination.
    trap 'code=$?; trap - EXIT; rm -f "$STATE/auto.enabled"; if [ "$code" -ne 0 ]; then : > "$MODDIR/disable"; rm -f "$CONFIG/enabled"; sync; fi; sh "$MODDIR/action.sh" _auto-off >> "$STATE/auto.log" 2>&1 || { echo "Restore failed; see module log"; code=1; }; rmdir "$STATE/auto.worker"; exit "$code"' EXIT
    trap 'exit 1' HUP INT TERM
    count=0
    while [ "$(getprop sys.boot_completed)" != 1 ]; do
      [ ! -f "$MODDIR/disable" ] && [ -f "$STATE/auto.enabled" ] || exit 0
      count=$((count + 1))
      [ "$count" -le 150 ] || fail 'Android boot did not complete within five minutes; auto disabled'
      sleep 2
    done
    (cd "$MODDIR" && sha256sum -c SHA256SUMS) > "$STATE/auto-checksums.log" || fail 'Module checksum failure'
    last= candidate= ready_at=0 attempts=0 checked_at=0
    while [ ! -f "$MODDIR/disable" ] && [ -f "$STATE/auto.enabled" ]; do
      [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || fail 'Module disabled or marked for removal'
      pid=$(pidof "$PACKAGE" || true)
      identity=
      case "$pid" in
        ''|*[!0-9]*) ;;
        *)
          started=$(sed 's/.*) //' "/proc/$pid/stat" 2>/dev/null | awk '{print $20}')
          [ -z "$started" ] || identity="$pid:$started"
          ;;
      esac
      now=$(cut -d . -f 1 /proc/uptime)
      if [ -n "$last" ] && [ "$identity" != "$last" ]; then
        if [ "$SYSTEM_SERVICE" = 1 ] || [ $((now - ready_at)) -lt 30 ]; then
          fail 'Target exited/restarted after patch; autostart disabled. Re-enable explicitly after investigation.'
        fi
        last=
      fi
      if [ -n "$identity" ] && [ "$identity" != "$last" ]; then
        if [ "$candidate" != "$identity" ]; then candidate=$identity; attempts=0; fi
        # An app can sit at its connection screen without loading OpenXR.
        # That is a normal waiting state, not a failed patch attempt.
        if [ "$attempts" -lt 2 ]; then attempts=$((attempts + 1)); fi
        if [ "$attempts" -ge 2 ] && grep -Fq "$LIBRARY" "/proc/$pid/maps" 2>/dev/null; then
          : > "$CONFIG/pending"
          sync
          if sh "$MODDIR/action.sh" _auto-on "$identity"; then :; else
            result=$?
            if [ "$result" = 2 ]; then sleep 2; continue; fi
            fail 'Guarded activation failed; auto stopped'
          fi
          last=$identity
          ready_at=$now
          checked_at=$now
        fi
      fi
      if [ -n "$last" ] && [ "$identity" = "$last" ] && [ $((now - checked_at)) -ge 30 ]; then
        if sh "$MODDIR/action.sh" status > "$STATE/auto-status.log"; then :; else
          result=$?
          if [ "$result" = 2 ]; then sleep 2; continue; fi
          fail 'Runtime verification failed; auto disabled'
        fi
        grep -Eq '^(MAIN|EXTRAPOLATION) pid=' "$STATE/auto-status.log" || fail 'Patch no longer active; auto disabled'
        # A reboot/kill during activation or this observation window leaves the
        # persistent marker behind, blocking the next automatic attempt.
        rm -f "$CONFIG/pending"
        sync
        checked_at=$now
      fi
      sleep 2
    done
    echo 'Autostart worker exited'
    ;;
  *) echo 'Usage: auto.sh enable|disable|status'; exit 1 ;;
esac
