#!/system/bin/sh
# Manager verifies restoration before marking removal. Early boot never calls
# Android services: a new kernel discarded the old RAM; same-boot pending state
# is an error and must not be mistaken for verified restoration.
MODDIR=${0%/*}
: > "$MODDIR/disable"
current=$(cat /proc/sys/kernel/random/boot_id)
saved=$(cat "$MODDIR/guard-boot-id" 2>/dev/null || true)
[ "$current" != "$saved" ] && exit 0
[ ! -f "$MODDIR/auto.pending" ] || { echo 'DISABLED - Error: removal was not prepared; restoration is unverified'; exit 1; }
echo 'DISABLED - No pending suite ownership.'
