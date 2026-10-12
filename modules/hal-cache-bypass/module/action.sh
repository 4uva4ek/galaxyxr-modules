#!/system/bin/sh
MODDIR=${0%/*}
case "${1:-report}" in
 on) exec sh "$MODDIR/auto.sh" enable ;;
 off) exec sh "$MODDIR/auto.sh" disable ;;
 status) exec sh "$MODDIR/auto.sh" status ;;
 report)
  echo 'Module status report'
  echo 'Cold kernel boot clears previous-boot interruption guards at startup; checkbox preference is unchanged. Soft reboot keeps guards.'
  echo 'Action checks status. It does not enable, stop or restart the module.'
  if [ -f "$MODDIR/disable" ]; then echo 'KernelSU checkbox: OFF'; else echo 'KernelSU checkbox: ON; runtime is checked separately'; fi
  sh "$MODDIR/auto.sh" status
  result=$?
  echo
  echo 'ENABLED means this module owns a verified live patch.'
  echo 'DISABLED means no verified module-owned activation; read any external/remaining-patch warning above.'
  echo 'KernelSU 1.0 saves the checkbox preference; changing it does not dispatch this Action.'
  echo 'Activation requires actual KernelSU late-load mode and a completed, stable soft-restart generation.'
  echo 'Cold-boot mode, absent ready state, busy restart or error means WAITING; the checkbox stays unchanged.'
  echo 'The root-terminal on command follows the same gate and cannot bypass it.'
  echo 'An existing worker observes disable; Action verifies the actual runtime state.'
  echo 'After an error or interrupted lease, resolve the cause, then use the explicit root-terminal on command to retry.'
  echo 'Within the same kernel boot, interrupted sessions remain quarantined across service or worker restarts.'
  echo 'If restoration is unverified, leave disabled and fully reboot the headset.'
  echo 'Full power removal clears the current RAM patch. This module stays waiting on cold boot until a verified late-load soft restart.'
  echo 'For persistent rollback, disable this module; a later authorized late-load soft restart can activate an enabled module again.'
  printf 'Root-terminal recovery: sh %s/action.sh off\n' "$MODDIR"
  printf 'Explicit activation/retry, only when intended: sh %s/action.sh on\n' "$MODDIR"
  echo 'Press Action again to verify restoration. Never restart the controller service to recover this patch.'
  exit "$result" ;;
 *) echo 'DISABLED - Error: expected status, on or off'; exit 1 ;;
esac
