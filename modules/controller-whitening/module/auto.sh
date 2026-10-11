#!/system/bin/sh
# Dedicated system-service lifecycle. KernelSU disable/remove is the preference.
# A persistent lease is same-kernel-boot crash evidence, not an activation opt-in.
set -u
MODDIR=$(CDPATH= cd -P "${0%/*}" && pwd) || exit 1
STATE=/dev/gxr_controller_whitening
SAVE=$MODDIR/state
LEASE=$SAVE/active.lease
BIN=$MODDIR/bin/gxr-whitening
SERVICE=vendor.samsung.hardware.secxrcontroller@1.0-service
SERVICE_PATH=/vendor/bin/hw/$SERVICE
MODE=${1:-status}
umask 077

numeric() { case "$1" in ''|*[!0-9]*) return 1;; esac; }
start_of() {
 numeric "$1" && [ "$1" -gt 1 ] || return 1
 [ -r "/proc/$1/stat" ] || return 1
 fields=$(sed 's/.*) //' "/proc/$1/stat") || return 1
 [ "${fields%% *}" != Z ] || return 1
 printf '%s\n' "$fields" | awk '{print $20}'
}
same_process() { [ -n "$2" ] && [ "$(start_of "$1" 2>/dev/null)" = "$2" ]; }
native_alive() {
 same_process "$1" "$2" && [ "$(readlink "/proc/$1/exe" 2>/dev/null)" = "$BIN" ]
}
worker_alive() {
 [ -f "$STATE/worker/pid" ] || return 1
 read -r WP WS < "$STATE/worker/pid" || return 1
 same_process "$WP" "$WS"
}
allowed() { [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ]; }
restart_requested() { [ -f "$STATE/restart-request" ]; }
fail() {
 printf 'DISABLED - Error: %s\n' "$*"
 : > "$MODDIR/disable"
 printf '%s\n' "$*" > "$SAVE/failure"
 sync
 return 1
}
setup() {
 [ "$(id -u)" = 0 ] || { echo 'DISABLED - Error: root required'; return 1; }
 awk '$2=="/dev" && $3=="tmpfs" {ok=1} END {exit !ok}' /proc/mounts || return 1
 for path in "$STATE" "$SAVE" "$LEASE" "$STATE/worker" "$MODDIR/disable"; do
  [ ! -L "$path" ] || { echo 'DISABLED - Error: unsafe state symlink'; return 1; }
 done
 mkdir -p "$STATE" "$SAVE" || return 1
 BOOT=$(cat /proc/sys/kernel/random/boot_id) || return 1
 [ -n "$BOOT" ] || return 1
}
load_lease() {
 [ -f "$LEASE" ] || return 1
 read -r LB LP LS LO LOS LW LWS EXTRA < "$LEASE" || return 1
 [ -n "$LB" ] && [ -z "${EXTRA:-}" ] || return 1
 for number in "$LP" "$LS" "$LO" "$LOS" "$LW" "$LWS"; do numeric "$number" || return 1; done
}
write_lease() {
 printf '%s %s %s %s %s %s %s\n' "$BOOT" "$TP" "$TS" "$1" "$2" "$$" "$MY_START" > "$LEASE.tmp.$$" &&
 mv -f "$LEASE.tmp.$$" "$LEASE" && sync
}
target() {
 TP=$(pidof "$SERVICE" 2>/dev/null || true)
 case "$TP" in '') return 2;; *[!0-9]*) return 1;; esac
 [ "$(tr '\000' '\n' < "/proc/$TP/cmdline" 2>/dev/null | head -n 1)" = "$SERVICE_PATH" ] || return 1
 TS=$(start_of "$TP") && [ -n "$TS" ]
}
query() { timeout 2 "$BIN" "$1" "$2"; }
stock_for_lease() {
 # A vanished exact process cannot retain a private memory patch. Never select
 # its replacement for restoration, even if the numeric PID has been reused.
 [ "$LB" = "$BOOT" ] && same_process "$LP" "$LS" || return 0
 result=$(query "$LP" status 2>&1) || { printf '%s\n' "$result"; return 1; }
 printf '%s\n' "$result" | grep -q '^STOCK '
}
stop_owned() {
 [ -f "$LEASE" ] || return 0
 load_lease || { [ ! -f "$LEASE" ] && return 0; fail 'Invalid ownership lease; leave disabled and reboot'; return 1; }
 if [ "$LB" = "$BOOT" ] && native_alive "$LO" "$LOS"; then
  # Only our exact executable/PID/start identity receives a signal. In
  # particular, no generic off operation is sent to an external experiment.
  kill -TERM "$LO" 2>/dev/null || true
  # SIGSTOP can prevent pending SIGTERM from being handled. Continue only the
  # same verified native helper, never the controller service or its threads.
  if native_alive "$LO" "$LOS"; then kill -CONT "$LO" 2>/dev/null || true; fi
 fi
 count=0
 while [ "$LB" = "$BOOT" ] && native_alive "$LO" "$LOS"; do
  count=$((count+1))
  [ "$count" -le 8 ] || { fail 'Owned helper did not exit; leave disabled and fully reboot'; return 1; }
  sleep 1
 done
 # The native guardian may still be completing restoration after owner death.
 count=0
 while ! stock_for_lease; do
  count=$((count+1))
  [ "$count" -lt 3 ] || { fail 'Stock restoration is unverified; leave disabled and fully reboot'; return 1; }
  sleep 1
 done
 rm -f "$LEASE"
 sync
 return 0
}
report() {
 if [ -f "$SAVE/failure" ]; then printf 'Last failure: '; cat "$SAVE/failure"; fi
 target; found=$?
 if [ "$found" = 2 ]; then
  echo 'DISABLED - Waiting: controller service is absent; no live target'
  return 0
 fi
 [ "$found" = 0 ] || { echo 'DISABLED - Error: controller service identity is ambiguous'; return 1; }
 result=$(query "$TP" status 2>&1) || { echo 'DISABLED - Error: runtime verification failed'; printf '%s\n' "$result"; return 1; }
 case "$result" in
  STOCK\ *)
   if allowed && worker_alive; then
    echo 'DISABLED - Waiting: worker is preparing guarded activation'
   elif allowed; then
    echo 'DISABLED - Waiting: checkbox ON; startup awaits the standard KernelSU service stage (soft reboot)'
   else
    echo 'DISABLED - Stock instructions verified'
   fi ;;
  WHITENING_ACTIVE\ *)
   if load_lease && [ "$LB" = "$BOOT" ] && [ "$LP:$LS" = "$TP:$TS" ] && native_alive "$LO" "$LOS" && worker_alive && [ "$LW:$LWS" = "$WP:$WS" ]; then
    if allowed; then echo 'ENABLED - Module-owned whitening patch verified'; else echo 'DISABLED - Stopping: owned patch is still active; restoration pending'; fi
   else
    echo 'DISABLED - External or unowned whitening patch is ACTIVE; it was not adopted or restored'
    echo 'Stop the standalone experiment before enabling this module; if ownership is uncertain, reboot with the module disabled'
   fi ;;
  *) echo 'DISABLED - Error: partial or unknown runtime patch; leave disabled and reboot'; printf '%s\n' "$result"; return 1 ;;
 esac
}
cleanup_worker() {
 code=$?
 trap - EXIT HUP INT TERM
 if [ "$code" -ne 0 ] && [ ! -f "$SAVE/failure" ]; then fail 'Worker failed or was interrupted; automatic retry is blocked' || true; fi
 # Covers a failure between launching the native child and recording its
 # identity in the durable lease. Native parent-death recovery is additional.
 if [ -n "${OWNER:-}" ] && native_alive "$OWNER" "${OWNER_START:-}"; then kill -TERM "$OWNER" 2>/dev/null || true; fi
 stop_owned || code=1
 if [ -n "${OWNER:-}" ] && ! same_process "$OWNER" "${OWNER_START:-}"; then wait "$OWNER" 2>/dev/null || true; fi
 rm -f "$STATE/worker/pid"
 rmdir "$STATE/worker" 2>/dev/null || true
 [ "${LAUNCH_LOCK:-0}" != 1 ] || rmdir "$STATE/launch" 2>/dev/null || true
 exit "$code"
}

if ! setup; then
 if [ "$MODE" != status ] && [ "$(id -u)" = 0 ] && [ ! -L "$MODDIR/disable" ]; then : > "$MODDIR/disable"; sync; fi
 echo 'DISABLED - Error: safe lifecycle setup failed'
 exit 1
fi
case "${1:-status}" in
 enable|resume) sh "$MODDIR/cold-boot-reset.sh" "$MODDIR" "$STATE" || exit $? ;;
esac
case "$MODE" in
 status) report ;;
 prepare-restart)
  # Explicit KernelSU notification precedes Android stop. Preserve preference;
  # let the existing owner restore, then wait for verified worker completion.
  : > "$STATE/restart-request"
  count=0
  while worker_alive; do
   count=$((count+1))
   [ "$count" -le 12 ] || { fail 'Planned restart: worker restoration timed out'; exit 1; }
   sleep 1
  done
  stop_owned || exit 1
  echo 'DISABLED - Planned restart: owned patch restored; preference preserved.'
  ;;
 disable)
  : > "$MODDIR/disable"
  sync
 count=0
 while [ -d "$STATE/launch" ]; do
  if ! worker_alive; then rmdir "$STATE/launch" 2>/dev/null || true; fi
  [ -d "$STATE/launch" ] || break
  count=$((count+1))
  [ "$count" -le 6 ] || { fail 'Launch transaction is stuck; leave disabled and reboot'; exit 1; }
  sleep 1
 done
  stop_owned || exit 1
  count=0
  while worker_alive; do
   count=$((count+1))
   [ "$count" -le 8 ] || { fail 'Worker exit is unconfirmed; restoration was checked but restart is required'; exit 1; }
   sleep 1
  done
  report
  ;;
 enable|resume)
  [ ! -f "$MODDIR/remove" ] || { fail 'Module is marked for removal'; exit 1; }
  if [ "$MODE" = resume ]; then allowed || exit 0; fi
  if worker_alive; then
   [ "$MODE" != enable ] || rm -f "$MODDIR/disable"
   report
   exit $?
  fi
  # Within one kernel boot, a service/daemon restart never retries an interrupted active
  # lease. A later explicit action.sh on can recover verified stock first.
  if [ -f "$LEASE" ]; then
   if [ "$MODE" = resume ]; then
    fail 'Interrupted active lease detected; automatic activation quarantined'
    stop_owned || true
    exit 1
   fi
   : > "$MODDIR/disable"
   stop_owned || exit 1
  fi
  [ "$MODE" != enable ] || rm -f "$MODDIR/disable" "$SAVE/failure"
  allowed || exit 0
  rm -f "$STATE/restart-request"
  if [ -d "$STATE/worker" ]; then
   rm -f "$STATE/worker/pid"
   rmdir "$STATE/worker" || { fail 'Unsafe stale worker directory'; exit 1; }
  fi
  [ ! -d "$STATE/launch" ] || rmdir "$STATE/launch" || { fail 'Unsafe stale launch directory'; exit 1; }
  mkdir "$STATE/worker" || { fail 'Concurrent worker startup'; exit 1; }
  nohup sh "$MODDIR/auto.sh" _run > "$STATE/worker.log" 2>&1 < /dev/null &
  child=$!
  child_start=$(start_of "$child")
  if [ -z "$child_start" ]; then fail 'Worker failed to start'; exit 1; fi
  printf '%s %s\n' "$child" "$child_start" > "$STATE/worker/pid"
  echo 'DISABLED - Waiting: automatic worker started; activation follows boot completion and guarded preflight'
  ;;
 _run)
  MY_START=$(start_of "$$")
  [ -n "$MY_START" ] || exit 1
  # Parent publishes this worker identity before it may acquire a lease.
  count=0
  until worker_alive && [ "$WP:$WS" = "$$:$MY_START" ]; do
   count=$((count+1)); [ "$count" -le 5 ] || exit 1; sleep 1
  done
  trap cleanup_worker EXIT
  trap 'exit 1' HUP INT TERM
  allowed || exit 0
  [ ! -f "$LEASE" ] || { fail 'Unexpected existing active lease'; exit 1; }
  count=0
  while [ "$(getprop sys.boot_completed)" != 1 ]; do
   allowed || exit 0
   restart_requested && exit 0
   count=$((count+1)); [ "$count" -le 150 ] || { fail 'Android boot did not complete in five minutes'; exit 1; }
   sleep 2
  done
  (cd "$MODDIR" && sha256sum -c SHA256SUMS) > "$STATE/checksums.log" 2>&1 || { fail 'Module checksum verification failed'; exit 1; }
  count=0
  while ! target; do
   allowed || exit 0
   restart_requested && exit 0
   count=$((count+1)); [ "$count" -le 60 ] || { fail 'Controller service is absent or ambiguous after boot'; exit 1; }
   sleep 1
  done
  EXPECTED_TARGET=$TP:$TS
  restart_requested && exit 0
  before=$(query "$TP" preflight 2>&1) || { printf '%s\n' "$before"; fail 'Guarded preflight failed'; exit 1; }
  printf '%s\n' "$before" | grep -q '^STOCK ' || { fail 'External whitening patch is active; stop the standalone experiment before enabling'; exit 1; }
  mkdir "$STATE/launch" || { fail 'Concurrent launch transaction'; exit 1; }
  LAUNCH_LOCK=1
  allowed || exit 0
  write_lease 0 0 || { fail 'Cannot durably record activation lease'; exit 1; }
  "$BIN" "$TP" hold > "$STATE/native.log" 2>&1 &
  OWNER=$!
  OWNER_START=$(start_of "$OWNER")
  [ -n "$OWNER_START" ] && write_lease "$OWNER" "$OWNER_START" || { fail 'Cannot record native owner identity'; exit 1; }
  allowed || exit 0
  rmdir "$STATE/launch" || { fail 'Cannot close launch transaction'; exit 1; }
  LAUNCH_LOCK=0
  # Native hold sets parent-death SIGTERM before application. Its independent
  # guardian recovers if the owner dies; the lease quarantines daemon restarts.
  sleep 1
  ticks=0
  while allowed; do
   if restart_requested; then
    echo 'DISABLED - Planned restart: restoring before Android stop.'
    exit 0
   fi
   native_alive "$OWNER" "$OWNER_START" || { fail 'Native owner exited; automatic retry is blocked'; exit 1; }
   target && [ "$TP:$TS" = "$EXPECTED_TARGET" ] || { fail 'Controller service exited or restarted; automatic retry is blocked'; exit 1; }
   if [ "$ticks" = 0 ]; then
    current=$(query "$TP" preflight 2>&1) || {
     printf 'HEALTH_ERROR uptime=%s boot_completed=%s target=%s:%s\n%s\n' "$(cat /proc/uptime)" "$(getprop sys.boot_completed)" "$TP" "$TS" "$current"
     # A request can arrive during the read-only query. Restoration in EXIT
     # remains mandatory; never keep an unverified patch active.
     restart_requested && exit 0
     fail 'Runtime health or calibration verification failed'; exit 1
    }
    printf '%s\n' "$current" | grep -q '^WHITENING_ACTIVE ' || { fail 'Patch is no longer active; automatic retry is blocked'; exit 1; }
   fi
   ticks=$(((ticks+1)%15))
   sleep 1
  done
  ;;
 *) echo 'DISABLED - Error: expected enable, resume, disable or status'; exit 1 ;;
esac
