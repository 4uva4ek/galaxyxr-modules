#!/system/bin/sh
set -eu
MODDIR=${0%/*}
BIN=$MODDIR/bin/gxr-fb10-gate
STATE=/dev/gxr-framebuffer10
umask 077
# Outside the native session directory: native owns that directory exclusively.
LOG=/dev/gxr-framebuffer10-hook-${1:-unknown}.log
exec >> "$LOG" 2>&1
printf '\nPHASE=%s GENERATION=%s UPTIME=%s\n' "${1:-unknown}" "${KSU_RESTART_GENERATION:-missing}" "$(cat /proc/uptime)"
trap 'rc=$?; set +x; printf "HOOK_EXIT=%s UPTIME=%s\n" "$rc" "$(cat /proc/uptime)"' EXIT
set -x
fail() {
 : > "$MODDIR/disable"
 printf '%s\n' "$*" > "$MODDIR/failure"
 echo "DISABLED - ERROR: $*"
 exit 1
}
# Waits are bounded by elapsed time, below KernelSU's per-hook limits
# (prepare/abort 30 s, stopped 45 s, started 70 s), so a slow status call can
# never let ksud kill the hook before it reports its own reason.
START=$(date +%s)
expired() { [ $(( $(date +%s) - START )) -ge "$1" ]; }
[ -n "${KSU_RESTART_GENERATION:-}" ] && [ -f /dev/ksu-restart-stage/busy ] || fail 'KernelSU coordinated restart required'
[ "$(cat /dev/ksu-restart-stage/generation)" = "$KSU_RESTART_GENERATION" ] || fail 'Restart generation mismatch'
case "$1" in
 prepare)
  sh "$MODDIR/cold-boot-reset.sh" "$MODDIR" "$STATE.guard" || exit $?
  (cd "$MODDIR" && sha256sum -c SHA256SUMS) || fail 'Package integrity mismatch'
  # The package ships no vendor code: generate (or verify) the color library
  # from this device's stock library before Android stops.
  "$BIN" --build-payload || fail 'Color library could not be generated from the stock library'
  chown 0:0 "$MODDIR/payload/libvulkan_compositor.so"
  chmod 0444 "$MODDIR/payload/libvulkan_compositor.so"
  chcon u:object_r:system_lib_file:s0 "$MODDIR/payload/libvulkan_compositor.so"
  if [ -d "$MODDIR/session-lease" ]; then
   status=$("$BIN" --status) || fail 'Previous session unverified'
   case "$status" in ENABLED*) "$BIN" --prepare-coordinated-restart;; *) fail 'Previous session failed or interrupted';; esac
  else
   [ ! -f "$MODDIR/failure" ] || fail 'Previous failure requires review'
   "$BIN" --preflight || fail 'Original baseline preflight failed'
  fi
  ;;
 stopped)
  while [ -d "$MODDIR/session-lease" ]; do
   ! expired 15 || fail 'Previous guardian did not hand off'
   sleep 1
  done
  [ ! -f "$MODDIR/disable" ] || fail 'Module disabled during preparation'
  nohup sh "$MODDIR/auto.sh" resume-coordinated > /dev/gxr-framebuffer10-launch.log 2>&1 < /dev/null &
  while [ ! -f "$STATE/start-ready" ] || [ "$(cat "$STATE/start-ready")" != "$KSU_RESTART_GENERATION" ]; do
   [ ! -f "$MODDIR/disable" ] || fail 'Native preparation failed; inspect launcher log'
   ! expired 38 || fail 'Native startup preparation timed out'
   sleep 1
  done
  echo 'DISABLED - Prepared: KernelSU may start Android once.'
  ;;
 started)
  while :; do
   status=$("$BIN" --status) || fail 'Native output verification failed'
   case "$status" in ENABLED*) printf '%s\n' "$status"; exit 0;; esac
   [ ! -f "$MODDIR/disable" ] || fail 'Native session stopped during verification'
   ! expired 58 || fail 'Output verification timed out'
   sleep 1
  done
  ;;
 abort)
  : > "$MODDIR/disable"
  if [ -d "$MODDIR/session-lease" ]; then
   "$BIN" --stop
   while [ -d "$MODDIR/session-lease" ] && ! expired 18; do
    sleep 1
   done
  fi
  if [ "$(getprop init.svc.surfaceflinger)" = stopped ]; then "$BIN" --preflight-stopped
  else "$BIN" --preflight; fi
  ;;
 *) fail 'Unknown restart phase';;
esac
