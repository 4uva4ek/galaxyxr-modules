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
[ -n "${KSU_RESTART_GENERATION:-}" ] && [ -f /dev/ksu-restart-stage/busy ] || fail 'KernelSU coordinated restart required'
[ "$(cat /dev/ksu-restart-stage/generation)" = "$KSU_RESTART_GENERATION" ] || fail 'Restart generation mismatch'
case "$1" in
 prepare)
  sh "$MODDIR/cold-boot-reset.sh" "$MODDIR" "$STATE.guard" || exit $?
  (cd "$MODDIR" && sha256sum -c SHA256SUMS) || fail 'Package integrity mismatch'
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
  count=0
  while [ -d "$MODDIR/session-lease" ]; do
   count=$((count+1)); [ "$count" -le 15 ] || fail 'Previous guardian did not hand off'
   sleep 1
  done
  [ ! -f "$MODDIR/disable" ] || fail 'Module disabled during preparation'
  nohup sh "$MODDIR/auto.sh" resume-coordinated > /dev/gxr-framebuffer10-launch.log 2>&1 < /dev/null &
  count=0
  while [ ! -f "$STATE/start-ready" ] || [ "$(cat "$STATE/start-ready")" != "$KSU_RESTART_GENERATION" ]; do
   [ ! -f "$MODDIR/disable" ] || fail 'Native preparation failed; inspect launcher log'
   count=$((count+1)); [ "$count" -le 20 ] || fail 'Native startup preparation timed out'
   sleep 1
  done
  echo 'DISABLED - Prepared: KernelSU may start Android once.'
  ;;
 started)
  count=0
  while :; do
   status=$("$BIN" --status) || fail 'Native output verification failed'
   case "$status" in ENABLED*) printf '%s\n' "$status"; exit 0;; esac
   [ ! -f "$MODDIR/disable" ] || fail 'Native session stopped during verification'
   count=$((count+1)); [ "$count" -le 60 ] || fail 'Output verification timed out'
   sleep 1
  done
  ;;
 abort)
  : > "$MODDIR/disable"
  if [ -d "$MODDIR/session-lease" ]; then
   "$BIN" --stop
   count=0
   while [ -d "$MODDIR/session-lease" ]; do
    count=$((count+1)); [ "$count" -le 20 ] || break
    sleep 1
   done
  fi
  if [ "$(getprop init.svc.surfaceflinger)" = stopped ]; then "$BIN" --preflight-stopped
  else "$BIN" --preflight; fi
  ;;
 *) fail 'Unknown restart phase';;
esac
