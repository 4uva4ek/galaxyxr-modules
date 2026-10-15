#!/system/bin/sh
# Galaxy XR MV-HEVC Eye Fix - GXR lifecycle entry (required by the manager's
# installed-lifecycle verification, KernelSU-GXR gxr_update.rs).
# Delegates to action.sh; run as root:
#   auto.sh status          report bind state
#   auto.sh on|apply|resume bind the patched library
#   auto.sh off|disable     unbind and restore the stock library
set -u
MODDIR=${0%/*}
case "${1:-status}" in
status) exec /system/bin/sh "$MODDIR/action.sh" status ;;
on|apply|resume) exec /system/bin/sh "$MODDIR/action.sh" on ;;
off|disable) exec /system/bin/sh "$MODDIR/action.sh" off ;;
*) echo 'usage: auto.sh status|on|off'; exit 64 ;;
esac
