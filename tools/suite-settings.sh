#!/system/bin/sh
# Explicit tuning only: this command never changes the KernelSU preference.
set -eu
MODDIR=$(CDPATH= cd -P "${0%/*}" && pwd)
. "$MODDIR/suite.conf"
umask 077
LOCK=/dev/${SUITE_ID}-settings.lock
case "${1:-get}" in
 get)
  for key in $SETTING_KEYS; do
   value=$(sed -n "s/^$key=//p" "$MODDIR/settings.conf" 2>/dev/null || true)
   case "$key" in
    TAIL_GAIN_MILLI) default=1000 ;;
    POSE_TIME_US|EXTRAPOLATION) default=0 ;;
    *) exit 1 ;;
   esac
   [ -n "$value" ] || value=$default
   printf '%s=%s\n' "$key" "$value"
  done ;;
 set)
  [ "$#" = 3 ] || exit 1
  key=$2 value=$3
  case " $SETTING_KEYS " in *" $key "*) ;; *) echo 'Unknown setting'; exit 1;; esac
  case "$value" in ''|*[!0-9]*) echo 'Expected an unsigned integer'; exit 1;; esac
  [ "${#value}" -le 5 ] || exit 1
  case "$key" in TAIL_GAIN_MILLI) max=1000;; POSE_TIME_US) max=30000;; EXTRAPOLATION) max=1;; *) exit 1;; esac
  [ "$value" -le "$max" ] || { echo 'Setting outside supported limits'; exit 1; }
  # Atomic, serialized update. Never source user-editable configuration as shell.
  [ ! -L "$MODDIR/settings.conf" ] || exit 1
  mkdir "$LOCK" 2>/dev/null || { echo 'Settings busy'; exit 2; }
  trap 'rm -f "$MODDIR/settings.tmp"; rmdir "$LOCK"' EXIT
  sh "$MODDIR/settings.sh" get | while IFS='=' read -r name old; do
   if [ "$name" = "$key" ]; then printf '%s=%s\n' "$name" "$value"; else printf '%s=%s\n' "$name" "$old"; fi
  done > "$MODDIR/settings.tmp"
  mv "$MODDIR/settings.tmp" "$MODDIR/settings.conf"
  echo 'Saved. The disabled module remains disabled.' ;;
 *) echo 'Expected get or set KEY INTEGER'; exit 1;;
esac
