#!/system/bin/sh
# Only stop/restore this module's verified owner. Never stop an external test.
MODDIR=${0%/*}
if ! sh "$MODDIR/auto.sh" disable; then
 echo 'DISABLED - Error: uninstall could not verify restoration. Fully reboot before using tracking.'
 exit 1
fi
