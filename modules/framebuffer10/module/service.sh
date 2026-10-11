#!/system/bin/sh
MODDIR=${0%/*}
[ -f "$MODDIR/disable" ] && exit 0
echo 'Framebuffer10 starts in KernelSU restart phases, not in service.sh.'
exec sh "$MODDIR/auto.sh" status
