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
# It also watches the power state. A real shutdown/reboot (sys.powerctl or
# sys.shutdown.requested set while sys.boot_completed is still 1) ends the
# session: the watcher writes the disable marker so the next boot shows the
# module OFF until it is enabled and activated again. KernelSU's emulated soft
# reboot (the Manager's soft restart) resets sys.boot_completed to 0, sets
# neither power property and sweeps module processes itself, so the checkbox is
# left exactly as the user set it and the relaunched service stage re-applies.
# Like the repository's autostart workers (Haptics MAIN), the watcher fails
# closed: an unexpected non-zero exit disables the module and detaches both
# mounts, so a dead watcher is never silently paired with a ticked checkbox.
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
# True while a real shutdown/reboot is in flight. sys.boot_completed=0 means a
# userspace/emulated soft reboot instead (KernelSU resets it first), and a
# userspace reason in either power property is never a full boot.
power_ending() {
    [ "$(/system/bin/getprop sys.boot_completed 2>/dev/null)" = 0 ] && return 1
    pctl=$(/system/bin/getprop sys.powerctl 2>/dev/null)
    sreq=$(/system/bin/getprop sys.shutdown.requested 2>/dev/null)
    case "$pctl" in ""|*userspace*) ;; *) return 0 ;; esac
    case "$sreq" in ""|*userspace*) ;; *) return 0 ;; esac
    return 1
}
end_for_full_boot() {
    : > "$M/disable" 2>/dev/null
    umount "$SF" 2>/dev/null || umount -l "$SF" 2>/dev/null
    umount "$AP" 2>/dev/null || umount -l "$AP" 2>/dev/null
    echo "$(date "+%Y-%m-%d %H:%M:%S") watcher: full reboot/shutdown detected (powerctl=$pctl shutdown.requested=$sreq); module disabled; enable and activate again to reapply" >> "$M/last-run.log" 2>/dev/null
    rm -f "$M/watcher.pid" 2>/dev/null
    exit 0
}
end_unexpected() {
    code=$?
    trap - EXIT
    if [ "$code" != 0 ]; then
        : > "$M/disable" 2>/dev/null
        umount "$SF" 2>/dev/null || umount -l "$SF" 2>/dev/null
        umount "$AP" 2>/dev/null || umount -l "$AP" 2>/dev/null
        echo "$(date "+%Y-%m-%d %H:%M:%S") watcher: unexpected exit ($code); module disabled; enable and activate again to reapply" >> "$M/last-run.log" 2>/dev/null
        rm -f "$M/watcher.pid" 2>/dev/null
    fi
    exit "$code"
}
trap end_unexpected EXIT
trap "power_ending && end_for_full_boot; exit 0" HUP INT TERM
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
    if power_ending; then
        end_for_full_boot
    fi
    sleep 3
done
' gxr-watch "$MODDIR" >/dev/null 2>&1 &
echo "$(date "+%Y-%m-%d %H:%M:%S") watcher started" >> "$MODDIR/last-run.log" 2>/dev/null
exit 0
