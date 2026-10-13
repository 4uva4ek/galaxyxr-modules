#!/system/bin/sh
# Galaxy XR OTA Control. Blocks or restores Samsung firmware updates.
#
# Blocked state: com.wssyncmldm and com.sec.android.soagent disabled for user 0
# plus global setting ota_disable_automatic_update = 1.
# Restored state: each package and the setting return to the value recorded in
# the baseline captured immediately before the first block. A package that was
# already disabled by the vendor is never enabled.
#
# Modes: boot, block, allow, status, report.
set -eu
MODDIR=${0%/*}
STATE=${GXR_OTA_STATE:-/data/adb/gxr-ota-control}
PACKAGES='com.wssyncmldm com.sec.android.soagent'
KEY=ota_disable_automatic_update
MODE=${1:-status}

# KernelSU runs module scripts in BusyBox ash with standalone mode on, where
# PATH order does not decide which command runs. Android's own tools are
# therefore resolved by absolute path first, with a PATH fallback; the
# GXR_OTA_* variables exist for host tests.
PM=${GXR_OTA_PM:-/system/bin/pm}
[ -x "$PM" ] || PM=pm
SETTINGS=${GXR_OTA_SETTINGS:-/system/bin/settings}
[ -x "$SETTINGS" ] || SETTINGS=settings
GETPROP=${GXR_OTA_GETPROP:-/system/bin/getprop}
[ -x "$GETPROP" ] || GETPROP=getprop

say() { printf '%s\n' "$*"; }
log() { printf '%s %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$*" >> "$STATE/log"; }
fail() {
 say "DISABLED - Error: $1"
 printf '%s\n' "$1" > "$STATE/failed"
 : > "$MODDIR/disable"
 exit 1
}
pkg_disabled() { "$PM" list packages -d 2>/dev/null | grep -qx "package:$1"; }
setting_value() { "$SETTINGS" get global "$KEY" 2>/dev/null | tr -d '\r'; }
boot_ready() { [ "$("$GETPROP" sys.boot_completed 2>/dev/null)" = 1 ]; }
was_applied() { [ "$(cat "$STATE/state" 2>/dev/null || true)" = blocked ]; }

# Record the untouched device state once, before the first change. Written
# through a temporary file so an interrupted run cannot leave a partial
# baseline behind.
record_baseline() {
 if [ -s "$STATE/baseline" ]; then
  return 0
 fi
 : > "$STATE/baseline.new"
 for p in $PACKAGES; do
  if pkg_disabled "$p"; then
   say "package:$p:disabled" >> "$STATE/baseline.new"
  else
   say "package:$p:enabled" >> "$STATE/baseline.new"
  fi
 done
 say "setting:$(setting_value)" >> "$STATE/baseline.new"
 chmod 600 "$STATE/baseline.new"
 mv "$STATE/baseline.new" "$STATE/baseline"
 log 'baseline recorded before the first block'
}

block() {
 if [ -f "$MODDIR/disable" ] || [ -f "$MODDIR/remove" ]; then
  say 'DISABLED - Error: the module is off or being removed; enable it first.'
  exit 1
 fi
 record_baseline
 for p in $PACKAGES; do
  if pkg_disabled "$p"; then
   continue
  fi
  "$PM" disable-user --user 0 "$p" >/dev/null 2>&1 || true
  if ! pkg_disabled "$p"; then
   fail "could not disable $p"
  fi
 done
 "$SETTINGS" put global "$KEY" 1 >/dev/null 2>&1 || true
 if [ "$(setting_value)" != 1 ]; then
  fail "could not set $KEY to 1"
 fi
 printf 'blocked\n' > "$STATE/state"
 rm -f "$STATE/failed"
 log "blocked: $PACKAGES disabled for user 0, $KEY=1"
}

allow() {
 if [ ! -s "$STATE/baseline" ]; then
  printf 'allowed\n' > "$STATE/state"
  log 'allowed: no baseline, nothing was applied'
  return 0
 fi
 for p in $PACKAGES; do
  if ! grep -qx "package:$p:enabled" "$STATE/baseline"; then
   continue
  fi
  if pkg_disabled "$p"; then
   "$PM" enable --user 0 "$p" >/dev/null 2>&1 || true
   if pkg_disabled "$p"; then
    fail "could not re-enable $p"
   fi
  fi
 done
 prior=$(sed -n 's/^setting://p' "$STATE/baseline" | head -n 1)
 current=$(setting_value)
 if [ "$prior" = null ] || [ -z "$prior" ]; then
  if [ "$current" != null ]; then
   "$SETTINGS" delete global "$KEY" >/dev/null 2>&1 || true
   current=$(setting_value)
   if [ "$current" != null ] && [ -n "$current" ]; then
    fail "could not clear $KEY, still $current"
   fi
  fi
 else
  if [ "$current" != "$prior" ]; then
   "$SETTINGS" put global "$KEY" "$prior" >/dev/null 2>&1 || true
   if [ "$(setting_value)" != "$prior" ]; then
    fail "could not restore $KEY to $prior"
   fi
  fi
 fi
 printf 'allowed\n' > "$STATE/state"
 rm -f "$STATE/failed"
 log 'allowed: packages and setting returned to the recorded baseline'
}

# True only when every package and the setting match the baseline and no
# failure is recorded. The updater uses this line before replacing the module.
is_stock() {
 if [ -f "$STATE/failed" ]; then
  return 1
 fi
 if [ ! -s "$STATE/baseline" ]; then
  return 0
 fi
 for p in $PACKAGES; do
  want=$(sed -n "s/^package:$p://p" "$STATE/baseline" | head -n 1)
  if [ "$want" = enabled ]; then
   if pkg_disabled "$p"; then
    return 1
   fi
  elif [ "$want" = disabled ]; then
   if ! pkg_disabled "$p"; then
    return 1
   fi
  else
   return 1
  fi
 done
 prior=$(sed -n 's/^setting://p' "$STATE/baseline" | head -n 1)
 current=$(setting_value)
 if [ "$prior" = null ] || [ -z "$prior" ]; then
  if [ "$current" != null ] && [ -n "$current" ]; then
   return 1
  fi
 elif [ "$current" != "$prior" ]; then
  return 1
 fi
 return 0
}

status() {
 for p in $PACKAGES; do
  if pkg_disabled "$p"; then
   say "package $p: disabled"
  else
   say "package $p: enabled"
  fi
 done
 say "$KEY: $(setting_value)"
 if [ -s "$STATE/baseline" ]; then
  say 'baseline:'
  sed 's/^/  /' "$STATE/baseline"
 fi
 if [ -f "$STATE/failed" ]; then
  say 'DISABLED - Error details:'
  cat "$STATE/failed"
 fi
 if [ -f "$STATE/log" ]; then
  tail -n 3 "$STATE/log"
 fi
 if [ -f "$MODDIR/disable" ] || [ -f "$MODDIR/remove" ]; then
  if is_stock; then
   say 'DISABLED - KernelSU preference OFF; nothing from this module is applied.'
  else
   say 'DISABLED - KernelSU preference OFF but the block is still live.'
   say "Restore now: sh $MODDIR/action.sh off"
  fi
 fi
 if is_stock; then
  say 'GXR_UPDATE_STATE=STOCK'
 fi
}

[ "$(id -u)" = 0 ] || { say 'DISABLED - Error: root is required.'; exit 1; }
mkdir -p "$STATE"
chmod 700 "$STATE"

case "$MODE" in
boot)
 # KernelSU does not run the scripts of a disabled module, so this stage
 # never restores an unticked module by itself: action.sh off is the
 # documented restore. A marker that appears while this stage is still
 # waiting is handled below, before anything is blocked.
 if [ -f "$MODDIR/remove" ]; then
  exit 0
 fi
 if [ -f "$MODDIR/disable" ]; then
  if was_applied; then
   log 'boot: block is live and the module is off; restoring'
   allow
  fi
  exit 0
 fi
 count=0
 log 'boot: waiting for sys.boot_completed'
 while ! boot_ready; do
  count=$((count + 1))
  if [ "$count" -gt 300 ]; then
   fail 'Android boot did not complete within 10 minutes'
  fi
  sleep 2
 done
 if [ -f "$MODDIR/remove" ]; then
  exit 0
 fi
 if [ -f "$MODDIR/disable" ]; then
  if was_applied; then
   log 'boot: module turned off while waiting; restoring'
   allow
  fi
  exit 0
 fi
 log 'boot: re-asserting the blocked state'
 block
 ;;
block)
 block
 ;;
allow)
 allow
 ;;
status|report)
 status
 ;;
*)
 say 'Expected boot, block, allow, status or report'
 exit 1
 ;;
esac
