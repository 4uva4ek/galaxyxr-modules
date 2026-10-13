#!/system/bin/sh
# KernelSU runs this in post-fs-data of the boot that removes the module,
# before system_server exists: pm and settings cannot run here, so nothing can
# be restored synchronously. The restore is handed to one process of this boot
# that waits for sys.boot_completed and then runs allow from a tmpfs copy,
# because ksud deletes this directory right after this script. Nothing
# survives the next reboot. action.sh off before removal remains the reliable
# path; this is the safety net.
MODDIR=${0%/*}
STATE=${GXR_OTA_STATE:-/data/adb/gxr-ota-control}
RUN=${GXR_OTA_RUN:-/dev/gxr-ota-control}
[ "$(id -u)" = 0 ] || exit 0
if [ ! -s "$STATE/baseline" ]; then
 [ -L "$STATE" ] || rm -rf "$STATE"
 exit 0
fi
[ ! -L "$RUN" ] || exit 0
mkdir -p "$RUN" && chmod 700 "$RUN" && cp "$MODDIR/ota-ctl.sh" "$RUN/restore.sh" || exit 0
nohup sh "$RUN/restore.sh" restore-after-boot >/dev/null 2>&1 &
exit 0
