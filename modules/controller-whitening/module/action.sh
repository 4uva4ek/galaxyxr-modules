#!/system/bin/sh
MODDIR=${0%/*}
case "${1:-report}" in
 on) exec sh "$MODDIR/auto.sh" enable ;;
 off) exec sh "$MODDIR/auto.sh" disable ;;
 status) exec sh "$MODDIR/auto.sh" status ;;
 report)
  echo 'Module status report'
  echo 'Action checks status. It does not enable, stop or restart the module.'
  if [ -f "$MODDIR/disable" ]; then echo 'KernelSU checkbox: OFF'; else echo 'KernelSU checkbox: ON; runtime is checked separately'; fi
  sh "$MODDIR/auto.sh" status
  result=$?
  echo
  echo 'ENABLED means this module owns a verified live patch.'
  echo 'DISABLED means no verified module-owned activation; read any external/remaining-patch warning above.'
  echo 'KernelSU 1.0 saves the checkbox preference; changing it does not dispatch this Action.'
  echo 'Enabled modules start at the standard KernelSU service stage; use the normal root soft-reboot flow.'
  echo 'An existing worker observes disable; Action verifies the actual runtime state.'
  echo 'After an error or interrupted lease, resolve the cause, then use the explicit root-terminal on command to retry.'
  echo 'Interrupted sessions are quarantined; service or worker restart never silently reapplies.'
  echo 'If restoration is unverified, leave disabled and fully reboot the headset.'
  echo 'A reboot while enabled is not a general rollback: a clean enabled module can start after Android boot completes.'
  printf 'Root-terminal recovery: sh %s/action.sh off\n' "$MODDIR"
  printf 'Explicit activation/retry, only when intended: sh %s/action.sh on\n' "$MODDIR"
  echo 'Press Action again to verify restoration. Never restart the controller service to recover this patch.'
  exit "$result" ;;
 *) echo 'DISABLED - Error: expected status, on or off'; exit 1 ;;
esac
