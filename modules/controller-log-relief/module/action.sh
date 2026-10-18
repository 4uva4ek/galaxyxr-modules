#!/system/bin/sh
MODDIR=${0%/*}
case "${1:-report}" in
 off) exec sh "$MODDIR/auto.sh" disable ;;
 status) exec sh "$MODDIR/auto.sh" status ;;
 report)
  echo 'Action reports status only. ENABLED requires verified module-owned runtime changes.'
  if [ -f "$MODDIR/disable" ]; then echo 'KernelSU preference: OFF'; else echo 'KernelSU preference: ON (not proof of activation)'; fi
  sh "$MODDIR/auto.sh" status
  result=$?
  echo 'Action reports the internal suite component; disable the parent suite to restore.'
  echo 'Checking the module starts it at the next standard KernelSU service stage / documented soft reboot.'
  echo 'Unchecking is observed by a running worker; allow up to 2 seconds for restoration.'
  printf 'Immediate recovery: sh %s/action.sh off
' "$MODDIR"
  echo 'If restoration fails: keep the module OFF, fully remove headset power and reboot.'
  echo 'Cold boot expires old guards without changing the checkbox; soft reboot preserves failure guards.'
  echo 'A reboot with the checkbox ON can reapply changes. Do not restart the controller service for recovery.'
  echo 'External LogLevel=0 is not adopted: end the standalone experiment before activation.'
  exit "$result" ;;
 *) echo 'DISABLED - Error: expected status or off'; exit 1 ;;
esac
