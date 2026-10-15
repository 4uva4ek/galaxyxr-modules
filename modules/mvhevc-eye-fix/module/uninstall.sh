#!/system/bin/sh
# Galaxy XR MV-HEVC Eye Fix - KernelSU removal entry. Restores the stock
# libraries immediately so removal leaves nothing patched behind, whether the
# module was enabled or disabled before deletion. Runs as root; harmless when
# nothing is bound.
set -u
MODDIR=${0%/*}
[ "$(id -u)" = 0 ] || exit 0
if ! /system/bin/sh "$MODDIR/action.sh" off >/dev/null 2>&1; then
    echo 'ERROR: could not restore the stock libraries during removal; reboot to finish.'
fi
exit 0
