#!/system/bin/sh
# One installed module; component views are private implementation state, never
# registered as modules. Their stop markers are derived from the suite preference.
set -u
MODDIR=$(CDPATH= cd -P "${0%/*}" && pwd) || exit 1
. "$MODDIR/suite.conf"
STATE=/dev/$SUITE_ID
VIEWS=$MODDIR/state/components
umask 077
allowed() { [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ]; }
fail() { : > "$MODDIR/disable"; printf '%s\n' "$*" > "$MODDIR/state/failure"; echo "DISABLED - Error: $*"; return 1; }
view() { printf '%s/%s' "$VIEWS" "$1"; }
stop_component() {
 v=$(view "$1")
 [ -f "$v/auto.sh" ] || return 0
 : > "$v/disable"
 sh "$v/auto.sh" disable
}
stop_all() {
 failed=0
 for component in $STOP_ORDER; do stop_component "$component" || failed=1; done
 [ "$failed" = 0 ] || { fail 'Component restoration failed; inspect Action and leave disabled'; return 1; }
}
prepare_views() {
 for component in $START_ORDER; do
  v=$(view "$component")
  [ ! -L "$v" ] && [ ! -L "$v/bin" ] || return 1
  mkdir -p "$v/bin"
  while read -r name; do
   case "$name" in ''|*..*|/*) return 1;; esac
   [ ! -L "$v/$name" ] || return 1
   source=$(printf '%s' "$name" | tr '/' '-')
   cp "$MODDIR/bin/$component--$source" "$v/$name" || return 1
   case "$name" in *.sh|bin/*) chmod 700 "$v/$name";; esac
  done < "$MODDIR/$component.files"
  : > "$v/disable"
  sh "$v/cold-boot-reset.sh" "$v" "/dev/${SUITE_ID}_$component" || return 1
 done
}
start_component() {
 v=$(view "$1")
 [ ! -f "$v/remove" ] || return 1
 rm -f "$v/disable"
 sh "$v/auto.sh" resume
}
# A KernelSU soft restart writes a new generation, creates busy, removes ready and
# resets sys.boot_completed. Read-only; no ksud call. START_GEN is the verified
# generation this worker activated under (Controller Suite only).
restart_pending() {
 stage=/dev/ksu-restart-stage
 [ "$(getprop sys.boot_completed)" = 1 ] || return 0
 [ ! -e "$stage/busy" ] && [ ! -e "$stage/error" ] || return 0
 [ "$(cat "$stage/ready" 2>/dev/null)" = ready ] || return 0
 [ "$(cat "$stage/generation" 2>/dev/null)" = "$START_GEN" ] || return 0
 return 1
}
# Any suite: ksud creates busy and writes a new generation first. BOOT_GEN is the
# generation present when this worker started (empty without the coordinator).
restart_started() {
 [ ! -e /dev/ksu-restart-stage/busy ] || return 0
 [ "$(cat /dev/ksu-restart-stage/generation 2>/dev/null)" = "$BOOT_GEN" ] || return 0
 return 1
}
settings() { sh "$MODDIR/settings.sh" get; }
tune() {
 current=$(settings) || return 1
 if [ "$SUITE_ID" = gxr_steamlink_suite ]; then
  lead=$(printf '%s\n' "$current" | sed -n 's/^POSE_TIME_US=//p')
  "$VIEWS/steamlink-pose-time/bin/gxr-steamlink-pose-time" lead set "$lead" >/dev/null || return 1
 fi
}
report() {
 if allowed; then echo 'KernelSU preference ON; checking component state.'; else echo 'DISABLED - KernelSU preference OFF.'; fi
 [ ! -f "$MODDIR/state/failure" ] || cat "$MODDIR/state/failure"
 for component in $START_ORDER; do
  v=$(view "$component")
  echo "--- $component ---"
  if [ -f "$v/action.sh" ]; then sh "$v/action.sh" status || return 1
  else echo 'DISABLED - Waiting: suite has not started.'; fi
 done
 if ! allowed && [ ! -d "$STATE/worker" ] && [ ! -f "$MODDIR/auto.pending" ]; then echo 'GXR_UPDATE_STATE=STOCK'; fi
}
[ "$(id -u)" = 0 ] || exit 1
awk '$2=="/dev" && $3=="tmpfs" {ok=1} END {exit !ok}' /proc/mounts || exit 1
for path in "$STATE" "$MODDIR/state" "$VIEWS"; do [ ! -L "$path" ] || exit 1; done
mkdir -p "$STATE" "$MODDIR/state"
case "${1:-status}" in
 status) report ;;
 _guard)
  owner=$2 stamp=$3
  while [ -d "$STATE/worker" ]; do
   actual=$(sed 's/.*) //' "/proc/$owner/stat" 2>/dev/null | awk '{print $20}')
   [ "$actual" = "$stamp" ] || break
   sleep 1
  done
  [ -d "$STATE/worker" ] || exit 0
  fail 'Suite supervisor exited unexpectedly; restoring components'
  if stop_all; then rm -f "$MODDIR/auto.pending"; fi
  rmdir "$STATE/worker" 2>/dev/null || true
  exit 1 ;;
 disable)
  : > "$MODDIR/disable"
  count=0
  while [ -d "$STATE/worker" ]; do
   count=$((count+1)); [ "$count" -le 45 ] || { fail 'Suite worker did not finish restoration'; exit 1; }
   sleep 1
  done
  stop_all || exit 1
  rm -f "$MODDIR/auto.pending"
  report ;;
 resume)
  sh "$MODDIR/cold-boot-reset.sh" "$MODDIR" "$STATE" || exit 1
  allowed || exit 0
  # A previous worker may still be finishing a planned-restart handoff. Wait for
  # it (bounded); a worker that remains is a live session, not a failure.
  count=0
  while [ -d "$STATE/worker" ] && [ "$count" -lt 30 ]; do count=$((count+1)); sleep 1; done
  [ ! -d "$STATE/worker" ] || { echo 'Suite worker already running; nothing started'; exit 0; }
  [ ! -f "$MODDIR/auto.pending" ] && [ ! -f "$MODDIR/state/failure" ] || { fail 'Interrupted or failed suite; reviewed recovery required'; exit 1; }
  mkdir "$STATE/worker" 2>/dev/null || exit 0
  nohup sh "$MODDIR/auto.sh" _run > "$STATE/worker.log" 2>&1 < /dev/null & ;;
 _run)
  cleanup() {
   result=$?; trap - EXIT HUP INT TERM
   # Keep the first recorded reason (for example which component failed).
   if [ "$result" != 0 ]; then
    if [ -f "$MODDIR/state/failure" ]; then : > "$MODDIR/disable"; else fail 'Suite worker failed; automatic retry blocked'; fi
   fi
   if stop_all; then rm -f "$MODDIR/auto.pending"; else result=1; fi
   rmdir "$STATE/worker" 2>/dev/null || true
   exit "$result"
  }
  trap cleanup EXIT
  trap 'exit 1' HUP INT TERM
  START_GEN=
  BOOT_GEN=$(cat /dev/ksu-restart-stage/generation 2>/dev/null || true)
  stamp=$(sed 's/.*) //' "/proc/$$/stat" | awk '{print $20}')
  nohup sh "$MODDIR/auto.sh" _guard "$$" "$stamp" > "$STATE/guardian.log" 2>&1 < /dev/null &
  for old in $REPLACES; do
   [ ! -d "/data/adb/modules/$old" ] || [ -f "/data/adb/modules/$old/remove" ] || { fail "Remove superseded module $old before activation"; exit 1; }
  done
  allowed || exit 0
  (cd "$MODDIR" && sha256sum -c SHA256SUMS) > "$STATE/checksums.log" || exit 1
  prepare_views || exit 1
  if [ ! -f "$MODDIR/settings.conf" ]; then
   settings > "$MODDIR/settings.initial" || exit 1
   mv "$MODDIR/settings.initial" "$MODDIR/settings.conf"
  fi
  if [ "$SUITE_ID" = gxr_controller_suite ]; then
   . "$VIEWS/hal-cache-bypass/gate.sh"
   while ! gate; do
    allowed || exit 0
    echo "DISABLED - Waiting: $GATE_REASON"
    sleep 2
   done
   START_GEN=$GATE_GEN
  else
   lead=$(settings | sed -n 's/^POSE_TIME_US=//p')
   "$VIEWS/steamlink-pose-time/bin/gxr-steamlink-pose-time" lead set "$lead" >/dev/null || exit 1
  fi
  : > "$MODDIR/auto.pending"
  for component in $START_ORDER; do
   allowed || exit 0
   start_component "$component" || exit 1
  done
  last=
  while allowed; do
   # Checked before component markers: components stop on the same restart.
   # Clean exit restores all components and clears auto.pending without a failure
   # guard, so the service stage after the restart starts the suite again.
   # A controller service exit/restart remains a component failure.
   if restart_started || { [ -n "$START_GEN" ] && restart_pending; }; then
    echo 'DISABLED - Planned restart: KernelSU restart started; restoring components; preference preserved'
    exit 0
   fi
   current=$(settings) || exit 1
   if [ "$current" != "$last" ]; then tune || exit 1; last=$current; fi
   for component in $START_ORDER; do
    [ ! -f "$VIEWS/$component/disable" ] || { fail "Component $component failed"; exit 1; }
   done
   sleep 1
  done ;;
 *) echo 'Expected resume, disable or status'; exit 1;;
esac
