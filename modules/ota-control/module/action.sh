#!/system/bin/sh
# KernelSU Action contract: the button reports only. Blocking and restoring are
# explicit root-shell commands so that a single button press cannot change the
# device update policy.
set -eu
MODDIR=${0%/*}
MODE=${1:-report}

case "$MODE" in
report)
 echo 'Galaxy XR OTA Control - status report only; Action never changes the block.'
 echo 'Blocked means both Samsung update clients are disabled for user 0 and ota_disable_automatic_update is 1.'
 echo 'Enable, disable or inspect the block from a root shell:'
 echo "  su -c 'sh $MODDIR/action.sh on'"
 echo "  su -c 'sh $MODDIR/action.sh off'"
 echo "  su -c 'sh $MODDIR/action.sh status'"
 echo 'on blocks updates now and turns the KernelSU preference ON; off restores the recorded baseline now and turns it OFF. Neither needs a reboot.'
 echo 'While the preference is ON the boot stage re-asserts the block. Unticking alone does not restore: run off, or remove the module and let the next boot finish.'
 echo 'ENABLED means the block is live. DISABLED means it is not applied, with a waiting or error reason.'
 echo "State directory: ${GXR_OTA_STATE:-/data/adb/gxr-ota-control}"
 exec sh "$MODDIR/ota-ctl.sh" report
 ;;
status)
 exec sh "$MODDIR/ota-ctl.sh" status
 ;;
on)
 if [ -f "$MODDIR/remove" ]; then
  echo 'DISABLED - Error: removal was requested; refusing to block updates.'
  exit 1
 fi
 rm -f "$MODDIR/disable"
 exec sh "$MODDIR/ota-ctl.sh" block
 ;;
off)
 : > "$MODDIR/disable"
 exec sh "$MODDIR/ota-ctl.sh" allow
 ;;
*)
 echo 'Expected report, status, on or off'
 exit 1
 ;;
esac
