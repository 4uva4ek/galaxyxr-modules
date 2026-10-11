#!/system/bin/sh
set -u
MODDIR=$(CDPATH= cd -P "${0%/*}" && pwd) || exit 1
BIN="$MODDIR/bin/gxr-fb10-gate"
LEASE="$MODDIR/session-lease"
STATE=/dev/gxr-framebuffer10
umask 077
allowed() { [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ]; }
fail() {
 : > "$MODDIR/disable" || echo 'DISABLED - ERROR: cannot save disable marker'
 printf '%s\n' "$*" > "$MODDIR/failure"
 sync
 echo "DISABLED - ERROR: $*"
 exit 1
}
case "${1:-status}" in
 # Native owns STATE exclusively: creating it means a session has begun.
 # Reset synchronization must never manufacture an incomplete native session.
 enable|resume|resume-coordinated) sh "$MODDIR/cold-boot-reset.sh" "$MODDIR" "$STATE.guard" || exit $? ;;
esac
case "${1:-status}" in
 status)
  result=0
  if allowed; then echo 'KernelSU checkbox: ON'; else echo 'KernelSU checkbox: OFF'; fi
  if [ -d "$STATE" ]; then
   "$BIN" --status || result=$?
  elif [ -d "$LEASE" ]; then
   echo 'DISABLED - Previous session was interrupted; automatic startup will quarantine it.'
  else echo 'DISABLED - Waiting: no module session; use the standard KernelSU service startup workflow.'; fi
  [ ! -f "$MODDIR/failure" ] || cat "$MODDIR/failure"
  exit "$result"
  ;;
 enable|resume|resume-coordinated)
  coordinated=0
  [ "${1:-}" != resume-coordinated ] || coordinated=1
  allowed || { echo 'DISABLED - KernelSU preference is OFF'; exit 0; }
  [ "$(id -u)" = 0 ] || fail 'Root required'
  for path in "$LEASE" "$MODDIR/disable" "$MODDIR/remove" "$MODDIR/failure"; do
   [ ! -L "$path" ] || fail 'Unsafe state symlink'
  done
  count=0
  while [ -d "$LEASE" ] && [ -f "$STATE/soft-restart" ]; do
   allowed || exit 0
   count=$((count+1)); [ "$count" -le 90 ] || fail 'Previous soft restart handoff did not complete'
   sleep 1
  done
  if [ -d "$LEASE" ]; then
   result=$("$BIN" --status) || fail 'Previous session is unverified; leave disabled and inspect recovery'
   case "$result" in ENABLED*) printf '%s\n' "$result"; exit 0;; esac
   fail 'Interrupted/previous session lease; automatic repetition blocked. Review logs before replacing the module disabled.'
  fi
  count=0
  while [ "$coordinated" = 0 ] && { [ "$(getprop sys.boot_completed)" != 1 ] || [ "$(getprop init.svc.bootanim)" != stopped ]; }; do
   allowed || exit 0
   count=$((count+1)); [ "$count" -le 90 ] || fail 'Framework startup timed out before activation'
   sleep 1
  done
  allowed || exit 0
  (cd "$MODDIR" && sha256sum -c SHA256SUMS) || fail 'Package integrity mismatch'
  chown 0:0 "$MODDIR/payload/libvulkan_compositor.so" &&
   chmod 0444 "$MODDIR/payload/libvulkan_compositor.so" &&
   chcon u:object_r:system_lib_file:s0 "$MODDIR/payload/libvulkan_compositor.so" || fail 'Cannot prepare separate library copy'
  preflight_arg=--preflight
  [ "$coordinated" = 0 ] || preflight_arg=--preflight-stopped
  preflight=$("$BIN" "$preflight_arg" 2>&1) || fail "$preflight"
  printf '%s\n' "$preflight"
  mkdir "$LEASE" 2>/dev/null || fail 'Concurrent or previous session; no repeat'
  cat /proc/sys/kernel/random/boot_id > "$LEASE/boot-id" || fail 'Cannot record boot identity'
  sync
  session_arg=--run-session
  [ "$coordinated" = 0 ] || session_arg=--run-coordinated-session
  "$BIN" "$session_arg"
  result=$?
  if [ "$result" -ne 4 ]; then : > "$MODDIR/disable" || fail 'Cannot save disable preference'; fi
  "$BIN" --status
  [ "$result" -eq 0 ] || [ "$result" -eq 4 ] || fail "Native session ended with code $result; inspect recovery or fully power-cycle"
  rm -f "$LEASE/boot-id"
  rmdir "$LEASE" || fail 'Recovered, but could not clear completed lease'
  sync
  if [ "$result" -eq 4 ]; then echo 'DISABLED - Soft restart completed; enabled preference preserved for the next service stage.'
  else echo 'DISABLED - Original library, 8-bit output and framework restored. Module preference is OFF.'; fi
  ;;
 disable)
  : > "$MODDIR/disable" || fail 'Cannot save disable preference'
  sync
  if [ -d "$LEASE" ] && [ -d "$STATE" ]; then
   "$BIN" --stop || fail 'Rollback request failed; fully power-cycle'
   echo 'DISABLED - Restoration requested. Press Action after the interface returns to verify completion.'
  else echo 'DISABLED - No module-owned current-boot session; external tests unchanged.'; fi
  ;;
 *) echo 'DISABLED - ERROR: expected status, enable, resume or disable'; exit 64 ;;
esac
