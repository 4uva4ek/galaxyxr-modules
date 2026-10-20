#!/system/bin/sh
MODDIR=${0%/*}
STATE="$MODDIR/runtime"
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
    rm -f "$STATE/active" "$STATE/error"
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
# Wait a bounded time for the existing authorized debugging service. Do not
# enable persistent developer settings or change the device's USB configuration.
n=0
while [ "$(getprop init.svc.adbd)" != running ]; do
    [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || exit 0
    [ "$n" -lt 60 ] || fail 'adbd is not running; enable USB debugging first.'
    sleep 2
    n=$((n+1))
done
[ "$(getprop ro.adb.secure)" = 1 ] || fail 'Secure ADB authentication is not confirmed.'
OLD_PORT=$(getprop service.adb.tcp.port)
CHANGED=1
setprop service.adb.tcp.port 5555 || fail 'Cannot set TCP port.'
setprop ctl.restart adbd || fail 'Cannot restart adbd.'
n=0
until healthy; do
    [ "$n" -lt 15 ] || fail 'ADB TCP listener did not become ready.'
    sleep 1
    n=$((n+1))
done
touch "$STATE/active"
while [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ]; do
    sleep 3
    [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || break
    healthy || fail 'ADB TCP health check failed.'
done
