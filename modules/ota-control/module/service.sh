#!/system/bin/sh
# KernelSU runs this in the non-blocking late_start service stage, so the boot
# work is detached: that stage must return at once and must not depend on
# KernelSU waiting for this script.
MODDIR=${0%/*}
nohup sh "$MODDIR/ota-ctl.sh" boot >/dev/null 2>&1 &
