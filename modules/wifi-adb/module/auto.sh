#!/system/bin/sh
MODDIR=${0%/*}
case "${1:-status}" in
 resume|enable) exec sh "$MODDIR/service.sh" ;;
 disable) exec sh "$MODDIR/adb-ctl.sh" off ;;
 status) exec sh "$MODDIR/adb-ctl.sh" status ;;
 *) exit 1 ;;
esac
