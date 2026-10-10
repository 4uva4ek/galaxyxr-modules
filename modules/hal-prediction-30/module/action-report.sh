#!/system/bin/sh
echo 'HAL Prediction +30 ms - status report'
echo 'Action reports status only. Use the KernelSU checkbox to start or stop.'
if [ -f "$MODDIR/disable" ]; then echo 'KernelSU preference: DISABLED'; else echo 'KernelSU preference: ENABLED (runtime verified separately below)'; fi
code=0
sh "$MODDIR/action.sh" status || code=$?
[ "$code" = 0 ] || echo 'ERROR: runtime state could not be verified; do not infer activation from the checkbox.'
if [ -f "$MODDIR/auto.sh" ]; then
  if [ -f /dev/gxr_hal_prediction_30/run ] && [ -d /dev/gxr_hal_prediction_30/worker ]; then
    echo 'Automatic worker markers present; the runtime result above is authoritative.'
  else echo 'Automatic worker not running. If enabled, toggle OFF then ON to retry.'; fi
fi
[ ! -f "$MODDIR/last-error" ] || { echo 'Last error:'; cat "$MODDIR/last-error"; }
echo 'ENABLED means this +30 ms patch is applied, not that tracking quality is validated.'
echo 'DISABLED means this patch is absent. Waiting and verification errors are reported separately.'
echo 'Scope: shared controller service, both controllers and all consumers. Velocity correction is separate.'
echo 'Recovery: uncheck this module and press Action to verify DISABLED.'
echo 'If restoration fails: leave it unchecked, then reboot. Do not restart system services manually.'
echo 'An enabled automatic module may reapply after reboot. Reboot alone is not rollback.'
echo 'After resolving an error, toggle OFF then ON to retry. Live control requires KernelSU GXR.'
printf 'Root-terminal recovery: sh %s/action.sh off\n' "$MODDIR"
exit "$code"
