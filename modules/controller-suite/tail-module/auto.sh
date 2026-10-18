#!/system/bin/sh
set -u
MODDIR=${0%/*}
STATE=/dev/gxr_controller_suite_tail-velocity
BIN=$MODDIR/bin/gxr-tail-velocity
mkdir -p "$STATE"
target() { pidof vendor.samsung.hardware.secxrcontroller@1.0-service; }
case "${1:-status}" in
 status)
  pid=$(target) || { echo 'DISABLED - Waiting: controller service absent'; exit 0; }
  "$BIN" "$pid" preflight;;
 resume)
  [ ! -f "$MODDIR/disable" ] || exit 0
  sh "$MODDIR/cold-boot-reset.sh" "$MODDIR" "$STATE" || exit 1
  [ ! -f "$MODDIR/auto.pending" ] || exit 1
  mkdir "$STATE/worker" 2>/dev/null || exit 0
  nohup sh "$MODDIR/auto.sh" _run > "$STATE/worker.log" 2>&1 < /dev/null &;;
 _run)
  cleanup() {
   rc=$?; trap - EXIT HUP INT TERM
   [ "$rc" = 0 ] || : > "$MODDIR/disable"
   # Native parent-death guardian independently restores after interruption.
   if [ "$rc" = 0 ]; then rm -f "$MODDIR/auto.pending"; fi
   rmdir "$STATE/worker" 2>/dev/null || true
   exit "$rc"
  }
  trap cleanup EXIT
  trap 'exit 1' HUP INT TERM
  pid=$(target) || exit 1
  case "$pid" in ''|*[!0-9]*) exit 1;; esac
  boot=$(cat /proc/sys/kernel/random/boot_id)
  stamp=$(sed 's/.*) //' "/proc/$pid/stat" | awk '{print $20}')
  : > "$MODDIR/auto.pending"
  "$BIN" "$pid" module-hold "$boot" "$stamp"
  rc=$?
  # A clean native exit while the master is ON still means the component stopped.
  # Surface it to the suite so the other components are restored as well.
  root=/data/adb/modules/gxr_controller_suite
  if [ ! -f "$root/disable" ] && [ ! -f "$root/remove" ]; then exit 1; fi
  exit "$rc";;
 disable)
  : > "$MODDIR/disable"
  # Suite master is already disabled on shutdown; its native owner sees that
  # preference and restores. Never adopt or remove a foreign tail owner.
  count=0
  while [ -d "$STATE/worker" ]; do
   count=$((count+1)); [ "$count" -le 8 ] || { echo 'DISABLED - Error: tail owner still running'; exit 1; }
   sleep 1
  done
  pid=$(target) || exit 0
  text=$("$BIN" "$pid" preflight) || exit 1
  printf '%s\n' "$text"
  printf '%s\n' "$text" | grep -q 'hooked=0' || exit 1
  rm -f "$MODDIR/auto.pending";;
 *) exit 1;;
esac
