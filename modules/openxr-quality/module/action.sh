#!/system/bin/sh
MODDIR=${0%/*}
case "${1:-report}" in
 off) exec sh "$MODDIR/auto.sh" off ;;
 status) exec sh "$MODDIR/auto.sh" status ;;
 report) ;;
 *) echo 'DISABLED - Error: expected report, off or status'; exit 1 ;;
esac
echo 'Module status report - Action does not activate or restart the module.'
if [ -f "$MODDIR/disable" ]; then echo 'KernelSU checkbox: OFF'; else echo 'KernelSU checkbox: ON (preference only)'; fi
"$MODDIR/bin/gxr-quality" status
result=$?
if [ -f "$MODDIR/state/failure" ]; then printf 'DISABLED - Recorded error: '; cat "$MODDIR/state/failure"; fi
if [ -f /dev/gxr-quality-session/status ]; then printf 'Last report (may be stale): '; cat /dev/gxr-quality-session/status; fi
echo
echo 'ENABLED reports a live controller: QUALITY held for an app, or waiting with ordinary policy active.'
echo 'Status checks controller identity; it does not independently read the vendor force latch.'
echo 'DISABLED means no live controller, or a stated error/waiting reason. Remaining-override warnings require recovery.'
echo 'Enable the checkbox, then use the documented KernelSU soft reboot when you intend activation.'
echo 'Verified late-load mode and stable restart markers are required. Cold boot cannot activate this system override.'
echo 'A running controller observes OFF and restores ordinary policy. Sleep/app exit also releases QUALITY.'
echo 'Thermal severity 4 releases the override; resumption requires severity 2 or lower.'
echo 'An actual failure disables the module; same-boot guards prevent automatic retries.'
echo 'Cold kernel boot clears previous-boot guards at next startup, preserving checkbox preference.'
echo 'Recovery: leave OFF, fully remove power and restart. This clears runtime force state.'
printf 'Root-terminal stop: sh %s/auto.sh off\n' "$MODDIR"
printf 'If the controller is gone but ownership remains: %s/bin/gxr-quality restore\n' "$MODDIR"
echo 'A later intentional late-load soft reboot may activate an enabled module again.'
exit "$result"
