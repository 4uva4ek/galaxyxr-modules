#!/system/bin/sh
MODDIR=${0%/*}
nohup sh "$MODDIR/adb-ctl.sh" run >/dev/null 2>&1 &
