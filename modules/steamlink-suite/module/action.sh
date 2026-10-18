#!/system/bin/sh
MODDIR=${0%/*}
case "${1:-report}" in
 off) exec sh "$MODDIR/auto.sh" disable;;
 status) exec sh "$MODDIR/auto.sh" status;;
 report)
  echo 'Action reports state; it does not activate the suite.'
  sh "$MODDIR/auto.sh" status
  result=$?
  echo 'Use WebUI for tuning. KernelSU checkbox is the master activation preference.'
  echo 'Disable restores components in dependency order. If restoration fails, keep disabled and fully power-cycle.'
  echo 'System HAL changes require verified late-load restart; no cold-boot retry of a failing experiment.'
  exit "$result";;
 *) echo 'Expected status or off'; exit 1;;
esac
