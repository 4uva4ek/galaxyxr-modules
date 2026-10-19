#!/system/bin/sh
# Haptics MAIN autostart, sourced from auto.sh. Derived from the shared
# tools/autostart.sh with the same checkbox, restart and failure guards, but
# light while the patch is live: once the controller service is patched the
# worker only reads /proc with shell builtins every few seconds. It does not
# run getprop/pidof, rehash files or call sync until something changes.
set -eu
CONFIG=$MODDIR/autostart
SOFT_RESTART_AWARE=${SOFT_RESTART_AWARE:-0}
command -v target_pids > /dev/null 2>&1 || target_pids() { pidof "$PACKAGE" || true; }
TARGET_READY_TIMEOUT=${TARGET_READY_TIMEOUT:-0}
# Poll period while patched (checkbox/restart reaction time), first health
# check after activation and the period of later ones, in seconds.
STEADY_SLEEP=${STEADY_SLEEP:-5}
FIRST_CHECK=${FIRST_CHECK:-30}
HEALTH_INTERVAL=${HEALTH_INTERVAL:-300}
# Sudden Android restart (zygote/system_server restart without the KernelSU
# notice; init does not reset sys.boot_completed for it). The controller
# service is an app process: when it dies together with system_server, the
# framework restarted and the patch is applied again. When it dies alone the
# patch may be the cause and autostart stops, after RESTART_GRACE seconds in
# which system_server may still be going down. A restart soon after the patch,
# or too many of them, also stops autostart so a patch-induced restart cannot loop.
RESTART_GRACE=${RESTART_GRACE:-10}
RESTART_READY_TIMEOUT=${RESTART_READY_TIMEOUT:-120}
SUDDEN_MIN_PATCH_AGE=${SUDDEN_MIN_PATCH_AGE:-60}
SUDDEN_LIMIT=${SUDDEN_LIMIT:-3}
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
# Sets identity to PID:starttime, or empty when the process is gone. Builtins only.
read_identity() {
  identity=
  case "${1:-}" in ''|*[!0-9]*) return 0 ;; esac
  identity_pid=$1
  stat_line=
  { read -r stat_line < "/proc/$identity_pid/stat"; } 2>/dev/null || return 0
  # The command name may contain spaces; fields after it are plain numbers.
  stat_line=${stat_line##*) }
  set -f
  set -- $stat_line
  set +f
  [ "$#" -ge 20 ] || return 0
  shift 19
  identity=$identity_pid:$1
}
# Sets android to system_server's PID:starttime, or empty while it is not running.
read_android() {
  android_saved=$identity
  read_identity "$(pidof system_server || true)"
  android=$identity
  identity=$android_saved
}
read_now() {
  uptime_line=
  read -r uptime_line < /proc/uptime
  now=${uptime_line%%[. ]*}
}
[ "$(id -u)" = 0 ] || fail 'Root required'
awk '$2 == "/dev" && $3 == "tmpfs" { ok=1 } END { exit !ok }' /proc/mounts || fail '/dev must be tmpfs'
[ ! -L "$STATE" ] || fail 'Unsafe state path'
umask 077
mkdir -p "$STATE"
[ ! -L "$CONFIG" ] || { echo 'Unsafe config path'; exit 1; }
case "${1:-status}" in
 enable|resume) sh "$MODDIR/cold-boot-reset.sh" "$MODDIR" "$STATE" || exit $? ;;
esac
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
    echo 'Automatic worker started. Future startup requires the KernelSU service stage.'
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
    # Module checksums and the pinned APK/JNI hashes are verified by
    # action.sh before every write, so the worker does not hash anything itself.
    # The pending marker needs no sync: it guards restarts within one kernel
    # boot, where the page cache keeps it, and cold-boot-reset.sh clears it
    # after a cold boot anyway.
    last= last_pid= last_android= identity= candidate= ready_at=0 attempts=0 checked_at=0 check_after=$FIRST_CHECK ready_wait_since=
    suspect_since= restart_reported=0 sudden=0 ready_budget=$TARGET_READY_TIMEOUT
    while [ ! -f "$MODDIR/disable" ] && [ -f "$STATE/auto.enabled" ]; do
      [ ! -f "$MODDIR/remove" ] || fail 'Module disabled or marked for removal'
      # Steady state: same patched process and no restart notice.
      if [ -n "$last" ] && [ ! -f "$STATE/soft-restart" ]; then
        read_identity "$last_pid"
        if [ "$identity" = "$last" ]; then
          read_now
          if [ $((now - checked_at)) -ge "$check_after" ]; then
            if sh "$MODDIR/action.sh" _auto-check "$identity" > "$STATE/auto-status.log"; then :; else
              result=$?
              # 2: another action holds the lock; 3: the process changed before the read.
              if [ "$result" = 2 ] || [ "$result" = 3 ]; then sleep 2; continue; fi
              if [ "$SOFT_RESTART_AWARE" = 1 ] && [ "$(getprop sys.boot_completed)" != 1 ]; then sleep 2; continue; fi
              fail 'Runtime verification failed; auto disabled'
            fi
            if ! grep -q '^MAIN pid=' "$STATE/auto-status.log"; then
              if [ "$SOFT_RESTART_AWARE" = 1 ] && [ "$(getprop sys.boot_completed)" != 1 ]; then sleep 2; continue; fi
              fail 'Patch no longer active; auto disabled'
            fi
            # A reboot/kill during activation or this observation window leaves the
            # persistent marker behind, blocking the next automatic attempt.
            rm -f "$CONFIG/pending"
            checked_at=$now
            check_after=$HEALTH_INTERVAL
          fi
          sleep "$STEADY_SLEEP"
          continue
        fi
      fi
      planned=0
      if [ "$SOFT_RESTART_AWARE" = 1 ]; then
        count=0
        while [ "$(getprop sys.boot_completed)" != 1 ]; do
          ready_wait_since=
          [ ! -f "$MODDIR/disable" ] && [ -f "$STATE/auto.enabled" ] || exit 0
          [ "$count" != 0 ] || echo 'WAITING: Android is restarting; no activation attempted'
          count=$((count + 1))
          [ "$count" -le 150 ] || fail 'Android restart did not complete within five minutes'
          sleep 2
        done
        if [ -f "$STATE/soft-restart" ]; then
          ready_wait_since=
          planned=1
          rm -f "$STATE/soft-restart"
          echo 'KernelSU soft restart completed; checking target identity'
        fi
      fi
      pid=$(target_pids)
      read_identity "$pid"
      read_now
      if [ -n "$last" ] && [ "$identity" != "$last" ]; then
        if [ "$SOFT_RESTART_AWARE" = 1 ] && [ "$(getprop sys.boot_completed)" != 1 ]; then sleep 2; continue; fi
        if [ "$planned" = 0 ]; then
          read_android
          [ -n "$suspect_since" ] || suspect_since=$now
          if [ -z "$android" ]; then
            # system_server is down: Android is restarting.
            [ "$restart_reported" = 1 ] || echo 'WAITING: controller service and system_server stopped; Android is restarting'
            restart_reported=1
            [ $((now - suspect_since)) -lt 300 ] || fail 'Android restart did not complete within five minutes'
            sleep 2; continue
          fi
          if [ "$android" = "$last_android" ]; then
            if [ $((now - suspect_since)) -lt "$RESTART_GRACE" ]; then sleep 2; continue; fi
            fail 'Target exited/restarted after patch; autostart disabled. Re-enable explicitly after investigation.'
          fi
          [ $((suspect_since - ready_at)) -ge "$SUDDEN_MIN_PATCH_AGE" ] ||
            fail "Android restarted within $SUDDEN_MIN_PATCH_AGE seconds of the patch; autostart disabled. Re-enable explicitly after investigation."
          sudden=$((sudden + 1))
          [ "$sudden" -le "$SUDDEN_LIMIT" ] ||
            fail "Android restarted without notice more than $SUDDEN_LIMIT times; autostart disabled. Re-enable explicitly after investigation."
          echo 'Android restarted without a KernelSU notice; the patch went with the old process and is applied again when the controller service is ready'
          ready_budget=$RESTART_READY_TIMEOUT
          ready_wait_since=
        fi
        # The old private mapping is gone. Only a restart notice or a restart
        # of the whole framework exempts the exit from failure containment.
        rm -f "$CONFIG/pending"
        last= last_pid= suspect_since= restart_reported=0
        candidate= attempts=0
      fi
      # Wait at most ready_budget seconds for this boot's controller service
      # and JNI mapping. Apply immediately when ready; no fixed delay.
      if [ "$TARGET_READY_TIMEOUT" -gt 0 ] && [ -z "$last" ]; then
        if [ -z "$ready_wait_since" ]; then
          ready_wait_since=$now
          echo "DISABLED - Waiting: controller service and JNI library, up to $ready_budget seconds after Android boot completion."
        fi
        if [ $((now - ready_wait_since)) -ge "$ready_budget" ]; then
          fail "Controller service/JNI readiness timed out after $ready_budget seconds; auto disabled"
        fi
      fi
      if [ -n "$identity" ] && [ "$identity" != "$last" ]; then
        if [ "$candidate" != "$identity" ]; then candidate=$identity; attempts=0; fi
        if [ "$attempts" -lt 2 ]; then attempts=$((attempts + 1)); fi
        [ "$TARGET_READY_TIMEOUT" -eq 0 ] || attempts=2
        if [ "$attempts" -ge 2 ] && grep -Fq "$LIBRARY" "/proc/$pid/maps" 2>/dev/null; then
          : > "$CONFIG/pending"
          if sh "$MODDIR/action.sh" _auto-on "$identity"; then :; else
            result=$?
            if [ "$result" = 2 ] || { [ "$SOFT_RESTART_AWARE" = 1 ] && [ "$result" = 3 ]; }; then
              rm -f "$CONFIG/pending"
              sleep 2; continue
            fi
            fail 'Guarded activation failed; auto stopped'
          fi
          last=$identity
          last_pid=$pid
          read_android
          last_android=$android
          ready_budget=$TARGET_READY_TIMEOUT
          ready_at=$now
          checked_at=$now
          check_after=$FIRST_CHECK
          ready_wait_since=
        fi
      fi
      # A process retained across a restart notice keeps its patch; the
      # steady-state branch above resumes its health checks on the next pass.
      sleep 2
    done
    echo 'Autostart worker exited'
    ;;
  *) echo 'Usage: auto.sh enable|disable|status'; exit 1 ;;
esac
