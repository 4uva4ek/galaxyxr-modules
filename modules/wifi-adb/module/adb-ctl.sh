#!/system/bin/sh
MODDIR=${0%/*}
STATE="$MODDIR/runtime"
umask 077
enabled() {
    [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ]
}
network_ready() {
    ip -4 -o addr show dev wlan0 scope global 2>/dev/null | grep -q ' inet '
}
waiting() {
    printf 'Waiting: %s\n' "$1" > "$STATE/waiting"
}
listen_ok() {
    awk '$2 ~ /:15B3$/ && $4 == "0A" { found=1 } END { exit !found }' \
        /proc/net/tcp /proc/net/tcp6 2>/dev/null
}
healthy() {
    [ "$(getprop service.adb.tcp.port)" = 5555 ] &&
    [ "$(getprop init.svc.adbd)" = running ] && listen_ok
}
report() {
    if [ -f "$STATE/active" ] && healthy; then
        echo 'ENABLED - ADB TCP listening on port 5555.'
        ip -4 addr show wlan0 2>/dev/null | awk '/inet / {split($2,a,"/"); print "Connect: adb connect " a[1] ":5555"}'
    else
        echo 'DISABLED - Module activation is absent or unconfirmed.'
    fi
    [ ! -f "$MODDIR/disable" ] || echo 'Startup preference: DISABLED.'
    [ ! -f "$STATE/error" ] || cat "$STATE/error"
    [ ! -f "$STATE/waiting" ] || cat "$STATE/waiting"
}
case "${1:-status}" in
 status) report; exit 0 ;;
 off)
    touch "$MODDIR/disable"
    n=0
    while [ -d "$STATE/lock" ] && [ "$n" -lt 15 ]; do sleep 1; n=$((n+1)); done
    if [ -d "$STATE/lock" ]; then
        echo 'DISABLED - Restoration unconfirmed; fully reboot with module disabled.'
        exit 1
    fi
    report
    exit 0 ;;
 run) ;;
 *) exit 1 ;;
esac
[ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || exit 0
mkdir -p "$STATE" || exit 1
chmod 700 "$STATE"
BOOT=$(cat /proc/sys/kernel/random/boot_id) || exit 1
if [ "$(cat "$STATE/boot" 2>/dev/null)" != "$BOOT" ]; then
    rm -f "$STATE/active" "$STATE/error" "$STATE/waiting" "$STATE/retries"
    rmdir "$STATE/lock" 2>/dev/null
    printf '%s\n' "$BOOT" > "$STATE/boot"
fi
mkdir "$STATE/lock" 2>/dev/null || exit 0
CHANGED=0
restore() {
    if [ "$CHANGED" = 1 ]; then
        if setprop service.adb.tcp.port "$OLD_PORT" && setprop ctl.restart adbd; then
            sleep 2
            if [ "$(getprop service.adb.tcp.port)" != "$OLD_PORT" ] ||
               [ "$(getprop init.svc.adbd)" != running ]; then
                echo 'ERROR: Restoration unconfirmed; fully reboot while disabled.' > "$STATE/error"
                touch "$MODDIR/disable"
            fi
        else
            echo 'ERROR: Restoration failed; fully reboot while disabled.' > "$STATE/error"
            touch "$MODDIR/disable"
        fi
    fi
    rm -f "$STATE/active"
    rm -f "$STATE/waiting"
    rmdir "$STATE/lock" 2>/dev/null
}
trap restore EXIT
trap 'touch "$MODDIR/disable"; exit 1' HUP INT TERM
fail() {
    printf 'ERROR: %s\n' "$1" > "$STATE/error"
    touch "$MODDIR/disable"
    exit 1
}
[ ! -f "$STATE/error" ] || fail 'Previous failure in this boot; inspect status and fully reboot while disabled.'
# Missing prerequisites are normal during boot or a Wi-Fi reconnect. Keep the
# preference enabled and never spend recovery attempts while offline.
wait_ready() {
    while enabled; do
        if [ "$(getprop sys.boot_completed)" != 1 ]; then
            waiting 'Android boot has not completed.'
        elif ! network_ready; then
            waiting 'Wi-Fi wlan0 has no IPv4 address.'
        elif [ "$(getprop init.svc.adbd)" != running ]; then
            waiting 'adbd is not running; USB debugging must be enabled.'
        else
            return 0
        fi
        sleep 2
    done
    return 1
}
wait_ready || exit 0
OLD_PORT=$(getprop service.adb.tcp.port)
RETRIES=$(cat "$STATE/retries" 2>/dev/null)
case "$RETRIES" in ''|*[!0-9]*) RETRIES=0 ;; esac
activate() {
    enabled || return 1
    [ "$(getprop ro.adb.secure)" = 1 ] || fail 'Secure ADB authentication is not confirmed.'
    CHANGED=1
    setprop service.adb.tcp.port 5555 || fail 'Cannot set TCP port.'
    setprop ctl.restart adbd || fail 'Cannot restart adbd.'
}
activate || exit 0
MISSES=0
while enabled; do
    if ! network_ready; then
        waiting 'Wi-Fi wlan0 has no IPv4 address.'
        MISSES=0
    elif healthy; then
        touch "$STATE/active"
        rm -f "$STATE/waiting"
        MISSES=0
    else
        rm -f "$STATE/active"
        waiting 'ADB TCP is starting or recovering.'
        MISSES=$((MISSES+1))
        # Allow 15 seconds for init/adbd to settle before restarting it.
        if [ "$MISSES" -ge 6 ]; then
            [ "$RETRIES" -lt 3 ] || fail 'ADB TCP recovery exhausted (3 attempts this kernel boot).'
            # Do not wait for adbd here: a stopped daemon is recoverable via init.
            enabled || break
            RETRIES=$((RETRIES+1))
            printf '%s\n' "$RETRIES" > "$STATE/retries"
            activate || break
            MISSES=0
        fi
    fi
    sleep 3
done
