#!/system/bin/sh
# No Android framework calls during post-fs-data. Volatile properties reset
# on a full boot; a live worker sees remove/disable and restores its baseline.
MODDIR=${0%/*}
touch "$MODDIR/disable"
