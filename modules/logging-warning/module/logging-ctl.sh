#!/system/bin/sh
MODDIR=${0%/*}
STATE="$MODDIR/runtime"
RESET=/data/adb/ksu/bin/resetprop
BOOT=$(cat /proc/sys/kernel/random/boot_id) || exit 1
strict() {
    case "$1" in
        W|w|WARN|WARNING|E|e|ERROR|F|f|FATAL|A|a|ASSERT|S|s|SILENT) return 0 ;;
        *) return 1 ;;
    esac
}
report() {
    if [ "$(cat "$STATE/boot" 2>/dev/null)" = "$BOOT" ] &&
       [ -f "$STATE/active" ] && [ -d "$STATE/lock" ] &&
       kill -0 "$(cat "$STATE/lock/pid" 2>/dev/null)" 2>/dev/null &&
       { strict "$(getprop log.tag)" || { [ -z "$(getprop log.tag)" ] && strict "$(getprop persist.log.tag)"; }; }; then
        echo 'ENABLED - Android WARNING threshold; per-tag overrides audited every 60 seconds.'
    else
        echo 'DISABLED - Runtime activation is absent or unconfirmed.'
    fi
    if [ -f "$MODDIR/disable" ]; then
        echo 'Startup preference: DISABLED.'
    else
        echo 'Startup preference: ENABLED.'
    fi
    [ ! -f "$STATE/error" ] || cat "$STATE/error"
    echo 'Scope: Android liblog properties; vendor bypasses, events and kernel logs remain unchanged.'
}
case "${1:-status}" in
 status) report; exit 0 ;;
 off)
    touch "$MODDIR/disable" || exit 1
    n=0
    while [ -d "$STATE/lock" ] && [ "$n" -lt 20 ]; do
        sleep 1
        n=$((n+1))
    done
    if [ "$(cat "$STATE/boot" 2>/dev/null)" = "$BOOT" ] &&
       { [ -d "$STATE/lock" ] || [ -s "$STATE/journal" ]; }; then
        echo 'DISABLED - Restoration unconfirmed; leave disabled and fully reboot.'
        exit 1
    fi
    report
    exit 0 ;;
 run) ;;
 *) exit 1 ;;
esac
[ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || exit 0
[ -x "$RESET" ] || { touch "$MODDIR/disable"; echo 'DISABLED - resetprop missing.'; exit 1; }
mkdir -p "$STATE" || { touch "$MODDIR/disable"; exit 1; }
chmod 700 "$STATE"
# Acquire before touching state. A stale lock from another cold boot is inert.
if [ -d "$STATE/lock" ] &&
   [ "$(cat "$STATE/boot" 2>/dev/null)" != "$BOOT" ]; then
    rm -f "$STATE/lock/pid"
    rmdir "$STATE/lock" 2>/dev/null
fi
mkdir "$STATE/lock" 2>/dev/null || exit 0
printf '%s\n' "$$" > "$STATE/lock/pid"
restore() {
    failed=0
    if [ -s "$STATE/journal" ]; then
        while IFS='|' read -r key present old; do
            current=$(getprop "$key")
            # Do not overwrite a value changed by another owner.
            if [ "$current" = W ]; then
                if [ "$present" = 1 ]; then
                    "$RESET" -n "$key" "$old" || failed=1
                    [ "$(getprop "$key")" = "$old" ] || failed=1
                else
                    "$RESET" -n --delete "$key" || failed=1
                    if getprop | grep -F "[$key]:" >/dev/null; then failed=1; fi
                fi
            elif [ "$current" != "$old" ]; then
                failed=1
            fi
        done < "$STATE/journal"
    fi
    rm -f "$STATE/active"
    if [ "$failed" = 0 ]; then
        rm -f "$STATE/journal"
    else
        echo 'ERROR: Restore conflict/failure; leave disabled and fully reboot.' > "$STATE/error"
        touch "$MODDIR/disable"
    fi
    rm -f "$STATE/lock/pid"
    rmdir "$STATE/lock"
}
trap restore EXIT
trap 'touch "$MODDIR/disable"; exit 1' HUP INT TERM
fail() {
    printf 'ERROR: %s\n' "$1" > "$STATE/error"
    touch "$MODDIR/disable"
    exit 1
}
if [ "$(cat "$STATE/boot" 2>/dev/null)" != "$BOOT" ]; then
    # Previous boot's RAM changes are gone. Never alter the checkbox here.
    rm -f "$STATE/journal" "$STATE/active" "$STATE/error"
    printf '%s\n' "$BOOT" > "$STATE/boot" || fail 'Cannot record boot identity.'
fi
# Never replay a half-finished transaction on the same boot.
if [ -s "$STATE/journal" ] || [ -f "$STATE/error" ]; then
    fail 'Previous same-boot transaction; disable and inspect, or fully reboot.'
fi
audit() {
    getprop > "$STATE/properties" || fail 'Cannot enumerate properties.'
    {
        echo log.tag
        sed -n 's/^\[\(log\.tag\.[^]]*\)\]:.*/\1/p; s/^\[persist\.\(log\.tag\.[^]]*\)\]:.*/\1/p' "$STATE/properties"
    } | sort -u > "$STATE/keys"
    while IFS= read -r key; do
        [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || return 0
        case "$key" in
            log.tag|log.tag.*) ;;
            *) fail 'Unexpected property namespace.' ;;
        esac
        current=$(getprop "$key")
        if awk -F '|' -v k="$key" '$1==k {found=1} END {exit !found}' "$STATE/journal" 2>/dev/null; then
            [ "$current" = W ] || fail "Property ownership changed: $key"
            continue
        fi
        # Preserve stricter existing thresholds (including SILENT).
        strict "$current" && continue
        if [ -z "$current" ] && strict "$(getprop "persist.$key")"; then continue; fi
        present=0
        if grep -F "[$key]:" "$STATE/properties" >/dev/null; then present=1; fi
        # Write-ahead journal: a crash never leaves an unrecorded mutation.
        printf '%s|%s|%s\n' "$key" "$present" "$current" >> "$STATE/journal" ||
            fail 'Cannot journal original property.'
        "$RESET" -n "$key" W || fail "Cannot set $key"
        [ "$(getprop "$key")" = W ] || fail "Cannot verify $key"
    done < "$STATE/keys"
}
audit
[ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || exit 0
touch "$STATE/active"
ticks=0
while [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ]; do
    sleep 5
    ticks=$((ticks+1))
    if [ "$ticks" -ge 12 ]; then
        audit
        ticks=0
    fi
done
