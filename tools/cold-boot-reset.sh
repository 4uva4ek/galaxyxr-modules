#!/system/bin/sh
# Expire interruption evidence only across a proven kernel boot boundary.
# No patching, process signals, preference changes or early-boot hook.
set -eu
MODDIR=$1
STATE=$2
die() { echo "DISABLED - Error: cold-boot guard reset: $*"; exit 1; }
uuid() { printf '%s\n' "$1" | grep -Eq '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$'; }
[ "$(id -u)" = 0 ] || die 'root required'
awk '$2=="/dev" && $3=="tmpfs" {ok=1} END {exit !ok}' /proc/mounts || die '/dev is not tmpfs'
BOOT=$(cat /proc/sys/kernel/random/boot_id) || die 'cannot read kernel boot identity'
uuid "$BOOT" || die 'invalid kernel boot identity'
# Validate every parent and leaf before changing any evidence.
for path in "$MODDIR" "$STATE" "$STATE/cold-reset" "$MODDIR/guard-boot-id" \
 "$MODDIR/autostart" "$MODDIR/autostart/pending" "$MODDIR/autostart/failed" \
 "$MODDIR/auto.pending" "$MODDIR/state" "$MODDIR/state/active.lease" \
 "$MODDIR/state/failure" "$MODDIR/session-lease" "$MODDIR/session-lease/boot-id" \
 "$MODDIR/failure"; do
 [ ! -L "$path" ] || die 'unsafe state symlink'
done
umask 077
mkdir -p "$STATE"
mkdir "$STATE/cold-reset" 2>/dev/null || { echo 'DISABLED - Waiting: another cold-boot reset is in progress'; exit 2; }
trap 'rmdir "$STATE/cold-reset"' EXIT
OLD=
[ ! -f "$MODDIR/guard-boot-id" ] || OLD=$(cat "$MODDIR/guard-boot-id")
if [ -n "$OLD" ]; then
 uuid "$OLD" || die 'invalid saved boot identity; evidence preserved'
 [ "$OLD" != "$BOOT" ] || exit 0
else
 # Legacy packages did not timestamp all guards. Do not guess whether an
 # untagged marker belongs to this boot. Fresh disabled replacements have none.
 for path in "$MODDIR/autostart/pending" "$MODDIR/autostart/failed" \
  "$MODDIR/auto.pending" "$MODDIR/state/active.lease" "$MODDIR/state/failure" \
  "$MODDIR/session-lease" "$MODDIR/failure"; do
  [ ! -e "$path" ] || die 'legacy evidence has no boot identity; use a reviewed disabled replacement'
 done
fi
if [ -n "$OLD" ]; then
 # These files refer only to private RAM changes. A new kernel boot destroys
 # the old owners/mappings. /dev locks and failure latches are already gone.
 # Never remove a directory recursively or touch disable/remove preferences.
 if [ -d "$MODDIR/session-lease" ]; then
  for path in "$MODDIR/session-lease/"* "$MODDIR/session-lease/".[!.]* "$MODDIR/session-lease/"..?*; do
   [ -e "$path" ] || continue
   [ "$path" = "$MODDIR/session-lease/boot-id" ] && [ -f "$path" ] || die 'unexpected session lease contents'
  done
  rm -f "$MODDIR/session-lease/boot-id"
  rmdir "$MODDIR/session-lease"
 fi
 rm -f "$MODDIR/autostart/pending" "$MODDIR/autostart/failed" \
  "$MODDIR/auto.pending" "$MODDIR/state/active.lease" "$MODDIR/state/failure" "$MODDIR/failure"
 echo 'Cold kernel boot confirmed: previous-boot interruption guards cleared; checkbox unchanged.'
fi
# Keep the old boot identity until all cleanup succeeds. This write happens
# before any new activation; a torn/invalid record fails closed on next entry.
printf '%s\n' "$BOOT" > "$MODDIR/guard-boot-id"
sync
