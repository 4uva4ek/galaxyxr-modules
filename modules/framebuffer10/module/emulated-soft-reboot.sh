#!/system/bin/sh
# Standard KernelSU stage, before Android stop. No second service writer.
MODDIR=${0%/*}
[ ! -f /dev/ksu-restart-stage/busy ] || exit 0
if ! "$MODDIR/bin/gxr-fb10-gate" --prepare-restart; then
 : > "$MODDIR/disable"
 echo 'Could not request orderly KernelSU restart handoff' > "$MODDIR/failure"
 echo 'DISABLED - ERROR: restart handoff failed; leave disabled and inspect recovery'
 exit 1
fi
