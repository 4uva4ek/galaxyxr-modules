#!/system/bin/sh
# Galaxy XR MV-HEVC Eye Fix - applied at the KernelSU service stage on every boot.
# Bind-mounts the patched libstagefright.so and libaplayer.so over the stock
# libraries only when this module is enabled. Fail-closed per library: refuses
# a library if it is not the exact reviewed stock build or if module checksums
# do not match.
set -u
MODDIR=${0%/*}
LOG="$MODDIR/last-run.log"

log() { echo "$(date '+%Y-%m-%d %H:%M:%S') $*" >> "$LOG"; }

apply() {
    src="$1"
    name="$2"
    expect="$3"
    pay="$MODDIR/bin/$name"
    if [ ! -f "$pay" ]; then log "$name payload missing; refuse"; return 1; fi
    s=$(sha256sum "$src" 2>/dev/null | cut -d' ' -f1)
    p=$(sha256sum "$pay" 2>/dev/null | cut -d' ' -f1)
    if [ "$s" = "$p" ]; then log "$name already bound"; return 0; fi
    if [ "$s" != "$expect" ]; then log "$src stock mismatch ($s); refuse"; return 1; fi
    chown 0:0 "$pay" && chmod 0444 "$pay" && chcon u:object_r:system_lib_file:s0 "$pay" \
        || { log "$name cannot prepare payload"; return 1; }
    if mount -o bind "$pay" "$src"; then
        log "$name bound over $src"
        return 0
    fi
    log "$name bind mount failed"
    return 1
}

# Restores the stock libraries when the module is turned off or removed while the
# headset runs. KernelSU runs no script on untick, so this detached watcher is the
# only path that can revert without a reboot. The loop lives in argv (sh -c), so it
# keeps working after the module directory is deleted under it.
start_watcher() {
    if [ -f "$MODDIR/watcher.pid" ] && [ -r "/proc/$(cat "$MODDIR/watcher.pid" 2>/dev/null)/cmdline" ]; then
        if tr '\0' ' ' < "/proc/$(cat "$MODDIR/watcher.pid")/cmdline" 2>/dev/null | grep -q gxr-watch; then
            return 0
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
        while [ "$n" -lt 3 ]; do
            umount "$SF" 2>/dev/null || umount -l "$SF" 2>/dev/null
            umount "$AP" 2>/dev/null || umount -l "$AP" 2>/dev/null
            ok=1
            [ "$(sha256sum "$SF" 2>/dev/null | cut -d" " -f1)" = 8db37c90c458e92faad1f0a6160d53e769da9759c6db14fb2cfedbcec1727490 ] || ok=0
            [ "$(sha256sum "$AP" 2>/dev/null | cut -d" " -f1)" = a1d055498e9bc9b09850748944f0605c7fd9052eec2050f027fa4724b449b047 ] || ok=0
            [ "$ok" = 1 ] && break
            n=$((n + 1))
            sleep 1
        done
        echo "$(date "+%Y-%m-%d %H:%M:%S") watcher: module off or removed; stock libraries restored (ok=$ok)" >> "$M/last-run.log" 2>/dev/null
        exit 0
    fi
    sleep 3
done
' gxr-watch "$MODDIR" >/dev/null 2>&1 &
    log 'watcher started'
}

# KernelSU does not dispatch this stage for a disabled module, but the manager's
# soft restart can. If it happens while the libraries are bound from this boot
# (module turned off or being removed), undo the patch instead of leaving it live.
if [ -f "$MODDIR/disable" ] || [ -f "$MODDIR/remove" ]; then
    log 'module disabled or removing; ensuring the stock libraries are in use'
    if ! /system/bin/sh "$MODDIR/action.sh" off >/dev/null 2>&1; then
        log 'restore during the disabled stage failed; reboot to finish'
    fi
    exit 0
fi
(cd "$MODDIR" && sha256sum -c SHA256SUMS >/dev/null 2>&1) || { log 'module checksum mismatch; refuse'; exit 1; }

rc=0
apply /system/lib64/libstagefright.so libstagefright.so 8db37c90c458e92faad1f0a6160d53e769da9759c6db14fb2cfedbcec1727490 || rc=1
apply /system/lib64/libaplayer.so libaplayer.so a1d055498e9bc9b09850748944f0605c7fd9052eec2050f027fa4724b449b047 || rc=1
start_watcher
exit $rc
