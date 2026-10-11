#!/system/bin/sh
# Standard KernelSU pre-restart notification. No patching or blocking here:
# this stage has a shared five-second deadline across all modules.
set -eu
MODDIR=${0%/*}
[ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || exit 0
MODULE_ID=$(sed -n 's/^id=//p' "$MODDIR/module.prop")
case "$MODULE_ID" in
  gxr_haptic_main) RUN=auto.enabled; WORKER=auto.worker ;;
  gxr_openxr_velocity_frame) RUN=run; WORKER=worker ;;
  *) exit 1 ;;
esac
STATE=/dev/$MODULE_ID
awk '$2 == "/dev" && $3 == "tmpfs" {ok=1} END {exit !ok}' /proc/mounts
[ ! -L "$STATE" ] && [ ! -L "$STATE/soft-restart" ] || exit 1
[ -f "$STATE/$RUN" ] && [ -d "$STATE/$WORKER" ] || exit 0
# Only an existing worker may consume this notice. It does not authorize a
# failed/stopped worker, clear a pending transaction, or change the checkbox.
: > "$STATE/soft-restart"
