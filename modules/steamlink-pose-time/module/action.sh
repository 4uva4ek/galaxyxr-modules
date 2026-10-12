#!/system/bin/sh
set -eu
MODDIR=${0%/*}
STATE=/dev/gxr_steamlink_pose_time
MODE=${1:-report}
case "$MODE" in
 off) exec sh "$MODDIR/auto.sh" disable ;;
 status|report) ;;
 *) echo 'DISABLED - Error: expected report, status or off'; exit 1 ;;
esac
if [ "$MODE" = report ]; then
 echo 'Steam Link Pose Time - status report only; Action does not enable the module.'
 echo 'Use the KernelSU checkbox, then the documented KernelSU soft reboot to start.'
 echo 'Disable to restore live hooks; if restoration fails, leave disabled and restart Steam Link.'
 echo 'Full reboot clears app RAM; an enabled module can reapply.'
 echo 'Requires Velocity Frame and the supported Steam Link 2.0.20 main package.'
fi
if [ "$MODE" = report ]; then
 "$MODDIR/bin/gxr-steamlink-pose-time" lead get 2>/dev/null | sed -n 's/^LEAD_MS=/Compensation (ms, 0-30; 28.82 = original label lead): /p'
fi
if [ -f "$MODDIR/disable" ] || [ -f "$MODDIR/remove" ]; then
 echo 'DISABLED - KernelSU preference OFF or removal requested.'
else
 echo 'KernelSU preference ON; checking live state below.'
fi
if [ -d "$STATE" ]; then
 result=0
 "$MODDIR/bin/gxr-steamlink-pose-time" status || result=$?
 if [ "$result" != 0 ]; then
  : > "$MODDIR/disable"
  "$MODDIR/bin/gxr-steamlink-pose-time" restore || echo 'DISABLED - Error: restore failed; restart Steam Link.'
  exit "$result"
 fi
else
 echo 'DISABLED - Waiting: worker has not started in this kernel boot.'
fi
# Manager calls off first. Never claim restoration while a worker or recovery
# record can still own hooks; old log lines are not the machine-readable state.
if [ -f "$MODDIR/disable" ] && [ ! -d "$STATE/worker" ] && [ ! -e "$STATE/target" ]; then
 echo 'GXR_UPDATE_STATE=STOCK'
fi
if [ "$MODE" = report ]; then
 [ ! -f "$STATE/failed" ] || { echo 'DISABLED - Error details:'; cat "$STATE/failed"; }
 [ ! -f "$STATE/auto.log" ] || tail -n 5 "$STATE/auto.log"
fi
exit 0
