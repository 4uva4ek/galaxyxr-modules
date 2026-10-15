#!/system/bin/sh
# KernelSU runs this during module removal. A disabled module's stage scripts are
# skipped, so this is the only automatic restore path left; run it
# unconditionally, because a block that failed halfway through still changed
# the device. allow only reverts what the recorded baseline says was enabled.
# After a verified restore the state directory is deleted too, so an uninstall
# leaves nothing of this module on the device; a failed restore keeps it for
# diagnosis (the README names the manual commands).
MODDIR=${0%/*}
STATE=${GXR_OTA_STATE:-/data/adb/gxr-ota-control}
[ "$(id -u)" = 0 ] || exit 0
if sh "$MODDIR/ota-ctl.sh" allow; then
    rm -rf "$STATE"
else
    echo 'DISABLED - Error: restore failed during removal; re-enable both update clients manually.'
fi
exit 0
