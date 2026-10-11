#!/system/bin/sh
MODDIR=${0%/*}
if [ -d "$MODDIR/session-lease" ]; then
 "$MODDIR/bin/gxr-fb10-gate" --stop || echo 'DISABLED - ERROR: rollback request failed; fully power-cycle headset.'
 echo 'DISABLED - Verify guardian rollback. Uninstall alone is not proof of 8-bit output; fully power-cycle if the library copy was already removed.'
else echo 'DISABLED - No module-owned session; external test state untouched.'; fi
