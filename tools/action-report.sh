#!/system/bin/sh
# Sourced by action.sh for the KernelSU Action button (no arguments).
echo 'Module status report'
echo 'This action checks status. It does not stop, start or restart the module.'
echo
if [ -f "$MODDIR/disable" ]; then
  echo 'KernelSU checkbox: OFF.'
else
  echo 'KernelSU checkbox: ON. Runtime status is checked separately below.'
fi
if [ -f "$MODDIR/auto.sh" ]; then
  REPORT_ID=$(sed -n 's/^id=//p' "$MODDIR/module.prop")
  REPORT_STATE=/dev/$REPORT_ID
  if { [ -f "$REPORT_STATE/run" ] && [ -d "$REPORT_STATE/worker" ]; } ||
     { [ -f "$REPORT_STATE/auto.enabled" ] && [ -d "$REPORT_STATE/auto.worker" ]; }; then
    echo 'Automatic worker: running markers present; see runtime status below.'
  else
    echo 'Automatic worker: NOT RUNNING. Checking the module alone does not start a worker.'
  fi
fi
echo
REPORT_CODE=0
REPORT_OUTPUT=$(sh "$MODDIR/action.sh" status 2>&1) || REPORT_CODE=$?
# Normalize native diagnostic labels only at the presentation boundary.
printf '%s\n' "$REPORT_OUTPUT" | sed -E 's/^(MAIN|CORRECTED|EXTRAPOLATION)([ :])/ENABLED\2/; s/^STOCK([ :])/DISABLED\1/; s/^WAITING[: ]*/DISABLED - Waiting: /'
[ "$REPORT_CODE" = 0 ] || echo "Verification failed (code $REPORT_CODE); runtime state could not be confirmed. See the reason above."
echo
echo 'ENABLED: the module patch is applied.'
echo 'DISABLED: the module patch is not applied.'
echo 'These states describe this module, not whether the entire feature works.'
echo
echo 'The KernelSU checkbox saves the startup preference; it does not call a module start/stop hook.'
echo 'A checked module starts at the next KernelSU service stage. On GXR 1.0 this follows the documented soft reboot after root.'
echo 'A running worker observes an unchecked module and attempts restoration. Press Action to verify.'
echo 'After an error, leave disabled, resolve the cause, then use the explicit recovery command below.'
echo 'For waiting targets, launch the application; the automatic worker applies the patch.'
echo 'Explicit root-terminal start or retry (also enables the saved preference):'
printf 'sh %s/action.sh on\n' "$MODDIR"
echo 'For the automatic variant, this also starts its background worker.'
echo 'Stop and restore original code:'
printf 'sh %s/action.sh off\n' "$MODDIR"
echo 'Press Action again to verify the result. Resolve compatibility/conflict errors before retrying.'
echo 'If restoration fails, leave disabled and restart the target app or fully reboot the headset.'
echo 'A checked module can apply again after startup; disable it before using reboot as rollback.'
exit "$REPORT_CODE"
