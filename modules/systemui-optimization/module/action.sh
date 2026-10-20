#!/system/bin/sh
MODDIR=${0%/*}
case "${1:-status}" in
    off|disable) sh "$MODDIR/auto.sh" disable || exit 1;;
    status) ;;
    *) echo 'Usage: action.sh status|off'; exit 1;;
esac
sh "$MODDIR/auto.sh" status || exit 1
echo 'Action reports status only. The KernelSU checkbox is the sole startup preference.'
echo 'After enabling, use the supported KernelSU module restart workflow.'
echo "Recovery: sh $MODDIR/auto.sh disable"
echo 'If restoration is unconfirmed, leave disabled and fully power off and reboot.'
