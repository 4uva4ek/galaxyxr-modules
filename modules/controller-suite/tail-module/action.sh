#!/system/bin/sh
MODDIR=${0%/*}
case "${1:-status}" in off) exec sh "$MODDIR/auto.sh" disable;; *) exec sh "$MODDIR/auto.sh" status;; esac
