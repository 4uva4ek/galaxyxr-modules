#!/system/bin/sh
# Conventional GXR lifecycle entry. The other modules and the release checks
# expect auto.sh resume|enable|disable|status; the logic lives in ota-ctl.sh and
# this file only maps the verbs. The KernelSU markers stay with action.sh.
MODDIR=${0%/*}
case "${1:-status}" in
 resume) exec sh "$MODDIR/ota-ctl.sh" boot ;;
 enable) exec sh "$MODDIR/ota-ctl.sh" block ;;
 disable) exec sh "$MODDIR/ota-ctl.sh" allow ;;
 status) exec sh "$MODDIR/ota-ctl.sh" status ;;
 *) echo 'DISABLED - Error: expected resume, enable, disable or status'; exit 1 ;;
esac
