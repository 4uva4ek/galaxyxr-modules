#!/system/bin/sh
# Galaxy XR MV-HEVC Eye Fix - detached revert watcher launcher.
# KernelSU runs no script when a module is unticked, so this watcher is the only
# path that can restore the stock libraries without a reboot. service.sh arms it
# at boot and on the manager soft restart; action.sh arms it after every apply
# (including a partial one), so any apply path re-arms it.
# The loop lives in argv (sh -c), so it keeps working after the module directory
# is deleted under it: it reacts to the disable/remove markers or a missing
# module directory, force-stops Gallery XR, unbinds both libraries, verifies the
# stock hashes, logs the verified result, removes its pid file and exits.
set -u
MODDIR=${0%/*}

# One watcher only: a live pid whose cmdline carries the gxr-watch marker wins.
# A dead or recycled pid fails the check and a fresh watcher starts.
if [ -f "$MODDIR/watcher.pid" ] && [ -r "/proc/$(cat "$MODDIR/watcher.pid" 2>/dev/null)/cmdline" ]; then
    if tr '\0' ' ' < "/proc/$(cat "$MODDIR/watcher.pid")/cmdline" 2>/dev/null | grep -q gxr-watch; then
        exit 0
    fi
fi
nohup /system/bin/sh -c '
M="$1"; SF=/system/lib64/libstagefright.so; AP=/system/lib64/libaplayer.so
echo $$ > "$M/watcher.pid" 2>/dev/null
while :; do
    if [ -f "$M/disable" ] || [ -f "$M/remove" ] || [ ! -d "$M" ]; then
        /system/bin/am force-stop com.samsung.android.galleryxr 2>/dev/null
        ok=0
        n=0
        while [ "$n" -lt 10 ]; do
            umount "$SF" 2>/dev/null || umount -l "$SF" 2>/dev/null
            umount "$AP" 2>/dev/null || umount -l "$AP" 2>/dev/null
            ok=1
            [ "$(sha256sum "$SF" 2>/dev/null | cut -d" " -f1)" = 8db37c90c458e92faad1f0a6160d53e769da9759c6db14fb2cfedbcec1727490 ] || ok=0
            [ "$(sha256sum "$AP" 2>/dev/null | cut -d" " -f1)" = a1d055498e9bc9b09850748944f0605c7fd9052eec2050f027fa4724b449b047 ] || ok=0
            [ "$ok" = 1 ] && break
            n=$((n + 1))
            sleep 1
        done
        if [ "$ok" = 1 ]; then
            echo "$(date "+%Y-%m-%d %H:%M:%S") watcher: module off or removed; stock libraries restored" >> "$M/last-run.log" 2>/dev/null
        else
            echo "$(date "+%Y-%m-%d %H:%M:%S") watcher: module off or removed; restore failed; reboot to finish" >> "$M/last-run.log" 2>/dev/null
        fi
        rm -f "$M/watcher.pid" 2>/dev/null
        exit 0
    fi
    sleep 3
done
' gxr-watch "$MODDIR" >/dev/null 2>&1 &
echo "$(date "+%Y-%m-%d %H:%M:%S") watcher started" >> "$MODDIR/last-run.log" 2>/dev/null
exit 0
