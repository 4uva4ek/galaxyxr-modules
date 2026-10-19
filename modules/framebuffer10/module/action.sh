#!/system/bin/sh
MODDIR=${0%/*}
case "${1:-report}" in
 off) exec sh "$MODDIR/auto.sh" disable ;;
 report|status)
  echo 'Module status report'
  echo 'Cold kernel boot clears previous-boot interruption guards at startup; checkbox preference is unchanged. Soft reboot keeps guards.'
  echo 'Action reports status only; it does not enable the module or restart services.'
  sh "$MODDIR/auto.sh" status
  result=$?
  echo 'ENABLED means the native controller verified the current compositor, color patch and 10-bit buffers.'
  echo 'DISABLED includes waiting, recovery and errors; read the reason. Checkbox state alone is not runtime proof.'
  echo 'Continuous 10-bit SDR output. No two-minute limit. This does not enable full HDR.'
  echo 'Check the module, then use standard KernelSU service startup (documented soft reboot after root).'
  echo 'Checkbox changes apply at the next KernelSU soft reboot (OFF restores stock 8-bit output there).'
  printf 'Immediate restore without reboot: sh %s/action.sh off (the Android interface restarts)\n' "$MODDIR"
  echo 'If recovery fails, fully remove headset power and reboot. An interrupted lease blocks automatic repetition in the same kernel boot.'
  echo 'After an error, inspect the report. A cold boot clears old guards at next startup without enabling a disabled checkbox.'
  echo 'Successful sessions can restart in the same boot. Standard KernelSU soft reboots preserve the enabled preference.'
  exit "$result"
  ;;
 *) echo 'DISABLED - ERROR: Action supports report/status/off only'; exit 64 ;;
esac
