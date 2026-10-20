#!/system/bin/sh
set -eu
MODDIR=${0%/*}
STATE=/dev/gxr_systemui_optimization
BIN="$MODDIR/bin/gxr-systemui"
PACKAGE=com.android.systemui
PIN=ab04d6daf57800a42170febaf33cb128441339c06bd6d46f068c0109693fee42
SPACE_PIN=7c72d2bd2284223877d5d5701fb69206fd2a202fa4867650b606c9245ff36fc5
SYSUI_PIN=d2060072c6046354d470a5e4949f762160a7d89f20310a8652e57c67805c0863
identity() {
    p=$(pidof "$PACKAGE" 2>/dev/null || true)
    case "$p" in ''|*[!0-9]*) return 1;; esac
    s=$(sed 's/.*) //' "/proc/$p/stat" 2>/dev/null | awk '{print $20}')
    [ -n "$s" ] || return 1
    printf '%s:%s\n' "$p" "$s"
}
restore() {
    [ -f "$STATE/target" ] || return 0
    old=$(cat "$STATE/target")
    now=$(identity || true)
    if [ "$old" = "$now" ]; then
        "$BIN" "${old%%:*}" off || {
            echo 'DISABLED - Restoration unconfirmed; full power cycle required.'
            return 1
        }
    else
        echo 'DISABLED - Original process exited; its private mapping is gone.'
    fi
    rm -f "$STATE/target"
}
[ "$(id -u)" = 0 ] || { echo 'DISABLED - Root required'; exit 1; }
awk '$2=="/dev" && $3=="tmpfs" {ok=1} END {exit !ok}' /proc/mounts || exit 1
for path in "$STATE" "$STATE/worker" "$STATE/target" "$MODDIR/autostart"; do
    [ ! -L "$path" ] || { echo 'DISABLED - Unsafe state path'; exit 1; }
done
umask 077
mkdir -p "$STATE"
case "${1:-status}" in
status)
    now=$(identity || true)
    if [ -n "$now" ]; then
        report=$("$BIN" "${now%%:*}" status) || {
            echo 'DISABLED - Runtime state could not be verified.'
            exit 1
        }
        printf '%s\n' "$report"
        case "$report" in
            DISABLED\ -*) echo 'GXR_UPDATE_STATE=STOCK';;
            ENABLED\ -*) ;;
            *) echo 'DISABLED - Unexpected runtime report'; exit 1;;
        esac
    else
        echo 'DISABLED - Waiting for SystemUI.'
        echo 'GXR_UPDATE_STATE=STOCK'
    fi
    if [ -f "$MODDIR/disable" ]; then echo 'Startup preference: DISABLED'
    else echo 'Startup preference: ENABLED'; fi
    [ ! -f "$MODDIR/autostart/failed" ] || cat "$MODDIR/autostart/failed"
    ;;
disable)
    : > "$MODDIR/disable"
    n=0
    while [ -d "$STATE/worker" ] && [ "$n" -lt 8 ]; do
        sleep 1; n=$((n+1))
    done
    [ ! -d "$STATE/worker" ] || {
        echo 'DISABLED - Worker did not exit; restoration unconfirmed. Fully power off and reboot.'
        exit 1
    }
    restore
    ;;
resume)
    [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || exit 0
    nohup sh "$MODDIR/auto.sh" run > "$STATE/worker.log" 2>&1 < /dev/null &
    ;;
run)
    [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || exit 0
    sh "$MODDIR/cold-boot-reset.sh" "$MODDIR" "$STATE" || { : > "$MODDIR/disable"; exit 1; }
    mkdir "$STATE/worker" 2>/dev/null || exit 0
    mkdir -p "$MODDIR/autostart"
    finish() {
        code=$?
        trap - EXIT HUP INT TERM
        if ! restore; then code=1; fi
        if [ "$code" -ne 0 ]; then
            : > "$MODDIR/disable"
            echo 'Activation/monitor/restoration failed; automatic retries blocked for this boot.' > "$MODDIR/autostart/failed"
        else
            rm -f "$MODDIR/autostart/pending"
        fi
        rmdir "$STATE/worker"
        exit "$code"
    }
    trap finish EXIT
    trap 'exit 1' HUP INT TERM
    [ ! -e "$MODDIR/autostart/pending" ] && [ ! -e "$MODDIR/autostart/failed" ] || {
        echo 'DISABLED - Previous interruption/failure; no retry this boot.'
        exit 1
    }
    (cd "$MODDIR" && sha256sum -c SHA256SUMS) > "$STATE/checksums.txt"
    [ "$(sha256sum /system_ext/lib64/libImpress-jni-vulkan.so | cut -d ' ' -f 1)" = "$PIN" ]
    [ "$(sha256sum /system_ext/lib64/libspaceflinger_jni_vulkan.so | cut -d ' ' -f 1)" = "$SPACE_PIN" ]
    [ "$(sha256sum /system_ext/lib64/libsysui_jni_soong_vulkan.so | cut -d ' ' -f 1)" = "$SYSUI_PIN" ]
    n=0
    while [ "$(getprop sys.boot_completed)" != 1 ]; do
        [ ! -f "$MODDIR/disable" ] || exit 0
        n=$((n+1)); [ "$n" -lt 150 ] || exit 1
        sleep 2
    done
    n=0
    while :; do
        [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ] || exit 0
        target=$(identity || true)
        if [ -n "$target" ] && "$BIN" "${target%%:*}" status > "$STATE/status.txt" 2>&1; then break; fi
        n=$((n+1)); [ "$n" -lt 30 ] || exit 1
        sleep 2
    done
    grep -q '^DISABLED ' "$STATE/status.txt" || { echo 'Unowned live patch; refusing activation'; exit 1; }
    : > "$MODDIR/autostart/pending"
    printf '%s\n' "$target" > "$STATE/target"
    [ "$(identity)" = "$target" ]
    "$BIN" "${target%%:*}" on
    while [ ! -f "$MODDIR/disable" ] && [ ! -f "$MODDIR/remove" ]; do
        [ "$(identity || true)" = "$target" ] || { echo 'SystemUI exited; no automatic reapplication'; exit 1; }
        "$BIN" "${target%%:*}" status > "$STATE/status.txt"
        grep -q '^ENABLED ' "$STATE/status.txt" || exit 1
        sleep 2
    done
    ;;
*) echo 'Usage: auto.sh status|resume|disable'; exit 1;;
esac
