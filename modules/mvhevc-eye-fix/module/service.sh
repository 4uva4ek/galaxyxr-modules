#!/system/bin/sh
# Galaxy XR MV-HEVC Eye Fix - applied at the KernelSU service stage on every boot.
# Generates the patched libstagefright.so and libaplayer.so from the device's own
# stock libraries and bind-mounts them over the stock paths, only when this module
# is enabled. No vendor binaries ship in the package. Fail-closed per library:
# refuses a library if it is not the exact reviewed stock build, if the generated
# result does not hash to the pinned value, or if module checksums do not match.
set -u
MODDIR=${0%/*}
LOG="$MODDIR/last-run.log"

log() { echo "$(date '+%Y-%m-%d %H:%M:%S') $*" >> "$LOG"; }

# Patched results are pinned in module/NOTICE.txt; generation is deterministic
# because the exact stock build is verified before any byte is written.
SF_PATCHED=4e51540d13f50d94f6c8deabae1137158af826cff29c56cd30ce4963a9ef1973
AP_PATCHED=017dc755311dfc9ff42d2501d4daa4c33ce92ec7d1d543c333a80c6f62d18ddd

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

apply() {
    src="$1"
    name="$2"
    expect="$3"
    patched="$4"
    pay="$MODDIR/bin/$name"
    s=$(sha256sum "$src" 2>/dev/null | cut -d' ' -f1)
    if [ "$s" = "$patched" ]; then log "$name already bound"; return 0; fi
    if [ -z "$s" ] || [ "$s" != "$expect" ]; then log "$src stock mismatch ($s); refuse"; return 1; fi
    if ! make_payload "$src" "$name" "$patched"; then log "$name payload generation failed or did not verify; refuse"; return 1; fi
    chown 0:0 "$pay" && chmod 0444 "$pay" && chcon u:object_r:system_lib_file:s0 "$pay" \
        || { log "$name cannot prepare payload"; return 1; }
    if mount -o bind "$pay" "$src"; then
        if [ "$(sha256sum "$src" 2>/dev/null | cut -d' ' -f1)" = "$patched" ]; then
            log "$name bound over $src"
            return 0
        fi
        umount "$src" 2>/dev/null || umount -l "$src" 2>/dev/null
        log "$name mounted copy failed verification; unmounted; refuse"
        return 1
    fi
    log "$name bind mount failed"
    return 1
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
apply /system/lib64/libstagefright.so libstagefright.so 8db37c90c458e92faad1f0a6160d53e769da9759c6db14fb2cfedbcec1727490 "$SF_PATCHED" || rc=1
apply /system/lib64/libaplayer.so libaplayer.so a1d055498e9bc9b09850748944f0605c7fd9052eec2050f027fa4724b449b047 "$AP_PATCHED" || rc=1
# Arms the detached revert watcher (module/watch-start.sh). KernelSU runs no
# script on untick, so the watcher is the only revert path without a reboot.
/system/bin/sh "$MODDIR/watch-start.sh" >/dev/null 2>&1
exit $rc
