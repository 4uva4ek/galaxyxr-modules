#!/system/bin/sh
# Galaxy XR MV-HEVC Eye Fix - control script. Runs as root; the KernelSU manager
# executes it for the module Action button and for its replacement checks
# (off, status); uninstall.sh and service.sh call it too. The patched libraries
# are generated from the device's own stock libraries; no vendor binaries ship.
#   (no argument)  manager Action / manual: apply if needed and report; on a disabled
#                  or removing module, restore the stock libraries
#   on             bind both patched libraries now
#   off            release both mounts, restore the stock libraries; prints
#                  GXR_UPDATE_STATE=STOCK only when the restoration is verified
#   status         report only
# Gallery XR is restarted after a successful apply so it loads the patched
# libraries, and stopped before an unbind so it releases its mappings.
# Universal report labels (docs/ACTION-REPORT.md): ENABLED while the patch is
# applied, DISABLED for everything else, with the reason on the same line. The
# GXR manager vetoes any status line containing "ACTIVE", so no line carries it.
set -u
MODDIR=${0%/*}
LOG="$MODDIR/last-run.log"
SF=/system/lib64/libstagefright.so
AP=/system/lib64/libaplayer.so
SF_HASH=8db37c90c458e92faad1f0a6160d53e769da9759c6db14fb2cfedbcec1727490
AP_HASH=a1d055498e9bc9b09850748944f0605c7fd9052eec2050f027fa4724b449b047
SF_PATCHED=4e51540d13f50d94f6c8deabae1137158af826cff29c56cd30ce4963a9ef1973
AP_PATCHED=017dc755311dfc9ff42d2501d4daa4c33ce92ec7d1d543c333a80c6f62d18ddd

log() { echo "$(date '+%Y-%m-%d %H:%M:%S') $*" >> "$LOG"; }

# Builds $MODDIR/bin/$name from the stock library $1. The caller has verified the
# stock hash, so the documented byte patches use fixed offsets (module/NOTICE.txt):
# the primary-eye condition byte becomes 'cinc ..,eq' (0x15) in both libraries and
# four bytes at 0xf568c become 'mov w3,#1' in libaplayer.so. The result must hash to
# the pinned value $3, or nothing is kept.
make_payload() {
    src="$1"
    name="$2"
    want="$3"
    pay="$MODDIR/bin/$name"
    if [ -f "$pay" ] && [ "$(sha256sum "$pay" 2>/dev/null | cut -d' ' -f1)" = "$want" ]; then
        return 0
    fi
    mkdir -p "$MODDIR/bin" 2>/dev/null || return 1
    tmp="$pay.gen.$$"
    rm -f "$MODDIR/bin/$name.gen."* 2>/dev/null
    cp -f "$src" "$tmp" || { rm -f "$tmp"; return 1; }
    case "$name" in
    libstagefright.so)
        # 0x1bbb20 + 1: cinc w23,w8,ne -> cinc w23,w8,eq
        printf '\025' | dd of="$tmp" bs=1 seek=1817377 count=1 conv=notrunc 2>/dev/null || { rm -f "$tmp"; return 1; }
        ;;
    libaplayer.so)
        # 0x233e18 + 1: cinc w23,w8,ne -> cinc w23,w8,eq
        printf '\025' | dd of="$tmp" bs=1 seek=2309657 count=1 conv=notrunc 2>/dev/null || { rm -f "$tmp"; return 1; }
        # 0xf568c: ldr w3,[sp,#0x24] -> mov w3,#1
        printf '\043\000\200\122' | dd of="$tmp" bs=1 seek=1005196 count=4 conv=notrunc 2>/dev/null || { rm -f "$tmp"; return 1; }
        ;;
    *)
        rm -f "$tmp"
        return 1
        ;;
    esac
    if [ "$(sha256sum "$tmp" 2>/dev/null | cut -d' ' -f1)" != "$want" ]; then
        rm -f "$tmp"
        return 1
    fi
    mv -f "$tmp" "$pay" || { rm -f "$tmp"; return 1; }
    return 0
}

# Android tools are not BusyBox applets and BusyBox ash standalone mode does not
# decide commands by PATH order, so `am` is resolved by absolute path first.
stop_gallery() {
    # Releases the library mappings: a bind mount with mapped files cannot be
    # detached, and a restarted Gallery XR picks up whichever library is live.
    if [ -x /system/bin/am ]; then
        /system/bin/am force-stop com.samsung.android.galleryxr 2>/dev/null || true
    else
        am force-stop com.samsung.android.galleryxr 2>/dev/null || true
    fi
}

# bound when the path serves the pinned patched build; stock in every other case,
# including a missing or unverified payload (nothing reviewed can be served then).
# The mount table is consulted as well, so a busy mount can never be mistaken for
# a restored library.
check() {
    src="$1"
    patched="$2"
    s=$(sha256sum "$src" 2>/dev/null | cut -d' ' -f1)
    if [ -n "$s" ] && [ "$s" = "$patched" ]; then echo bound; else echo stock; fi
}

bound_now() { grep -qF " $1 " /proc/self/mountinfo; }

apply_one() {
    src="$1"
    name="$2"
    patched="$3"
    pay="$MODDIR/bin/$name"
    if ! make_payload "$src" "$name" "$patched"; then
        echo "DISABLED - Error: $name payload generation failed or did not verify; refuse"
        return 1
    fi
    chown 0:0 "$pay" && chmod 0444 "$pay" && chcon u:object_r:system_lib_file:s0 "$pay" \
        || { echo "DISABLED - Error: $name cannot prepare payload"; return 1; }
    if mount -o bind "$pay" "$src"; then
        if [ "$(sha256sum "$src" 2>/dev/null | cut -d' ' -f1)" = "$patched" ]; then
            log "$name bound (action)"
            return 0
        fi
        umount "$src" 2>/dev/null || umount -l "$src" 2>/dev/null
        echo "DISABLED - Error: $name mounted copy failed verification; unmounted"
        return 1
    fi
    echo "DISABLED - Error: $name bind mount failed"
    return 1
}

# Unbind one library. A busy mount is retried, then detached lazily; success
# requires the mount table entry gone and a fresh read of the path back to the
# stock file.
unbind_one() {
    src="$1"
    patched="$2"
    tries=0
    while [ "$tries" -lt 3 ]; do
        if [ "$(check "$src" "$patched")" != bound ] && ! bound_now "$src"; then
            return 0
        fi
        umount "$src" 2>/dev/null || umount -l "$src" 2>/dev/null || sleep 1
        tries=$((tries + 1))
    done
    if bound_now "$src"; then return 1; fi
    [ "$(check "$src" "$patched")" = stock ]
}

unbind_all() {
    sf=$(check "$SF" "$SF_PATCHED")
    ap=$(check "$AP" "$AP_PATCHED")
    if [ "$sf" = bound ] || [ "$ap" = bound ] || bound_now "$SF" || bound_now "$AP"; then
        stop_gallery
    fi
    rc=0
    if ! unbind_one "$SF" "$SF_PATCHED"; then
        echo 'DISABLED - Error: libstagefright.so is still bound over the stock library; reboot to finish.'
        rc=1
    fi
    if ! unbind_one "$AP" "$AP_PATCHED"; then
        echo 'DISABLED - Error: libaplayer.so is still bound over the stock library; reboot to finish.'
        rc=1
    fi
    if [ "$rc" = 0 ]; then log 'stock libraries restored (unbind)'; fi
    return $rc
}

apply_all() {
    (cd "$MODDIR" && sha256sum -c SHA256SUMS >/dev/null 2>&1) || { echo 'DISABLED - Error: module checksum mismatch; refuse'; return 1; }
    sf=$(check "$SF" "$SF_PATCHED")
    ap=$(check "$AP" "$AP_PATCHED")
    if [ "$sf" = stock ]; then
        s=$(sha256sum "$SF" | cut -d' ' -f1)
        [ "$s" = "$SF_HASH" ] || { echo "DISABLED - Error: unexpected libstagefright.so ($s)"; return 1; }
    fi
    if [ "$ap" = stock ]; then
        s=$(sha256sum "$AP" | cut -d' ' -f1)
        [ "$s" = "$AP_HASH" ] || { echo "DISABLED - Error: unexpected libaplayer.so ($s)"; return 1; }
    fi
    [ "$sf" = bound ] || apply_one "$SF" libstagefright.so "$SF_PATCHED" || return 1
    [ "$ap" = bound ] || apply_one "$AP" libaplayer.so "$AP_PATCHED" || return 1
    stop_gallery
    return 0
}

report() {
    sf=$(check "$SF" "$SF_PATCHED")
    ap=$(check "$AP" "$AP_PATCHED")
    if [ "$sf" = bound ] && [ "$ap" = bound ]; then
        echo "ENABLED - patched libstagefright.so and libaplayer.so are bound"
    elif [ "$sf" = stock ] && [ "$ap" = stock ] && ! bound_now "$SF" && ! bound_now "$AP"; then
        echo "DISABLED - stock libraries in use; nothing from this module is applied"
        echo 'GXR_UPDATE_STATE=STOCK'
    else
        echo "DISABLED - Error: partial state (libstagefright=$sf libaplayer=$ap); run action.sh off"
    fi
}

case "${1:-}" in
status) report ;;
on)
    # Arm the revert watcher on every apply, including a partial one: KernelSU
    # runs no script on untick, so the watcher is the only revert path without a
    # reboot; it must exist wherever a mount can exist.
    if apply_all; then rc=0; else rc=1; fi
    /system/bin/sh "$MODDIR/watch-start.sh" >/dev/null 2>&1
    [ "$rc" = 0 ] && report
    ;;
off)
    if ! unbind_all; then
        exit 1
    fi
    echo 'DISABLED - stock libraries restored; restart Gallery XR to compare.'
    echo 'GXR_UPDATE_STATE=STOCK'
    ;;
'')
    # Manager Action button or a manual run. While the module is enabled this
    # applies if needed and reports; while it is disabled or being removed it
    # restores the stock libraries, so one press reverts a live patch.
    if [ -f "$MODDIR/disable" ] || [ -f "$MODDIR/remove" ]; then
        if ! unbind_all; then
            exit 1
        fi
        echo 'DISABLED - stock libraries restored; restart Gallery XR to compare.'
        echo 'GXR_UPDATE_STATE=STOCK'
    else
        apply_all
        # Same arming as the explicit on command above.
        /system/bin/sh "$MODDIR/watch-start.sh" >/dev/null 2>&1
        report
    fi
    ;;
*)
    echo 'usage: action.sh [status|on|off]'; exit 64
    ;;
esac
