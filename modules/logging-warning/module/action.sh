#!/system/bin/sh
MODDIR=${0%/*}
sh "$MODDIR/logging-ctl.sh" status
echo 'Action reports status only. The KernelSU checkbox is the sole activation preference.'
echo 'After enabling, use the supported KernelSU module restart workflow.'
echo "Recovery: sh $MODDIR/auto.sh disable"
echo 'If restoration is unconfirmed, leave disabled and fully power off and reboot.'
