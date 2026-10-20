#!/system/bin/sh
MODDIR=${0%/*}
exec sh "$MODDIR/logging-ctl.sh" off
