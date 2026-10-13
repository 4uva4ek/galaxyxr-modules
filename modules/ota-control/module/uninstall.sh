#!/system/bin/sh
# KernelSU runs this during module removal. A disabled module's scripts are
# skipped, so this is the only automatic restore path left; run it
# unconditionally, because a block that failed halfway through still changed
# the device. allow only reverts what the recorded baseline says was enabled.
MODDIR=${0%/*}
STATE=${GXR_OTA_STATE:-/data/adb/gxr-ota-control}
[ "$(id -u)" = 0 ] || exit 0
sh "$MODDIR/ota-ctl.sh" allow || echo 'DISABLED - Error: restore failed during removal; re-enable both update clients manually.'
exit 0
