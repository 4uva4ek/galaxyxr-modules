#!/system/bin/sh
# KernelSU runs this during module removal. Restore updates when the block is
# still live, so removing the module never leaves updates off by silence.
MODDIR=${0%/*}
STATE=${GXR_OTA_STATE:-/data/adb/gxr-ota-control}
[ "$(id -u)" = 0 ] || exit 0
if [ "$(cat "$STATE/state" 2>/dev/null)" = blocked ]; then
 sh "$MODDIR/ota-ctl.sh" allow || echo 'DISABLED - Error: restore failed during removal; re-enable both update clients manually.'
fi
exit 0
