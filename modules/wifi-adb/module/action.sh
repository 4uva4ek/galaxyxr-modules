#!/system/bin/sh
MODDIR=${0%/*}
case "${1:-status}" in
 on) rm -f "$MODDIR/disable"; sh "$MODDIR/service.sh" ;;
 off) sh "$MODDIR/adb-ctl.sh" off ;;
 status) sh "$MODDIR/adb-ctl.sh" status ;;
 *) exit 1 ;;
esac
echo 'Action reports runtime status; the checkbox controls startup.'
echo 'Enable in KernelSU, then restart modules using the supported KernelSU workflow.'
echo "Recovery: sh $MODDIR/action.sh off"
echo 'If restoration fails, leave disabled and fully reboot.'
