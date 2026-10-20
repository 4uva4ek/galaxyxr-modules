#!/system/bin/sh
MODDIR=${0%/*}
[ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || exit 0
nohup sh "$MODDIR/logging-ctl.sh" run >/dev/null 2>&1 &
