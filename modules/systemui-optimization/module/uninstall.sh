#!/system/bin/sh
MODDIR=${0%/*}
# Does not require Android services; safe in the pre-Android removal stage.
sh "$MODDIR/auto.sh" disable
