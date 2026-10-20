"""Host simulation: no Android commands or device access."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
BASH = 'C:/Program Files/Git/bin/bash.exe' if os.name == 'nt' else 'bash'

for source in (HERE / 'module').glob('*.sh'):
    subprocess.run([BASH, '-n', str(source)], check=True)

for scenario in ('success', 'insecure', 'restart_failure', 'late_wifi',
                 'late_boot', 'late_adbd', 'offline_disable', 'reconnect',
                 'daemon_reset', 'listener_failure'):
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        shutil.copytree(HERE / 'module', root / 'module')
        harness = root / 'test.sh'
        harness.write_text(r'''
set -eu
cd "$(dirname "$0")"
export TEST_ROOT="$PWD"
export MOD_TEST="$PWD/module"
printf '1234\n' > port
echo 0 > ticks
echo 0 > restarts
getprop() {
    case "$1" in
    sys.boot_completed)
        if [ "$SCENARIO" = late_boot ] && [ "$(cat "$TEST_ROOT/ticks")" -lt 90 ]; then echo 0; else echo 1; fi ;;
    service.adb.tcp.port) cat "$TEST_ROOT/port" ;;
    init.svc.adbd)
        if [ "$SCENARIO" = late_adbd ] && [ "$(cat "$TEST_ROOT/ticks")" -lt 90 ]; then echo stopped; else echo running; fi ;;
    ro.adb.secure) [ "$SCENARIO" = insecure ] && echo 0 || echo 1 ;;
    esac
}
setprop() {
    if [ "$1" = service.adb.tcp.port ]; then
        echo "$2" > "$TEST_ROOT/port"
    elif [ "$SCENARIO" = restart_failure ] && [ "$(cat "$TEST_ROOT/port")" = 5555 ]; then
        return 1
    else
        echo $(( $(cat "$TEST_ROOT/restarts") + 1 )) > "$TEST_ROOT/restarts"
    fi
}
cat() {
    if [ "$1" = /proc/sys/kernel/random/boot_id ]; then echo test-boot; else command cat "$@"; fi
}
ip() {
    tick=$(cat "$TEST_ROOT/ticks")
    case "$SCENARIO" in
    late_wifi|offline_disable) [ "$tick" -ge 90 ] || return 0 ;;
    reconnect) [ "$tick" -lt 1 ] || [ "$tick" -ge 90 ] || return 0 ;;
    esac
    echo '15: wlan0 inet 192.0.2.1/24 scope global wlan0'
}
awk() { [ "$SCENARIO" != listener_failure ] && [ "$(cat "$TEST_ROOT/port")" = 5555 ]; }
sleep() {
    tick=$(( $(cat "$TEST_ROOT/ticks") + 1 ))
    echo "$tick" > "$TEST_ROOT/ticks"
    [ "$tick" -lt 200 ] || exit 99
    if [ "$SCENARIO" = offline_disable ]; then
        [ "$tick" -lt 90 ] || touch "$MOD_TEST/disable"
        return
    fi
    if [ "$SCENARIO" = daemon_reset ] && [ "$tick" = 1 ]; then
        echo 0 > "$TEST_ROOT/port"
        return
    fi
    if [ "$SCENARIO" = daemon_reset ] && [ "$(cat "$TEST_ROOT/restarts")" -lt 2 ]; then return; fi
    if [ "$SCENARIO" = reconnect ] && [ "$tick" -le 90 ]; then return; fi
    if [ -f "$MOD_TEST/runtime/active" ]; then
        touch "$TEST_ROOT/observed-active" "$MOD_TEST/disable"
    fi
}
export -f getprop setprop cat ip awk sleep
bash "$MOD_TEST/adb-ctl.sh" run && result=0 || result=$?
[ "$(cat port)" = 1234 ]
[ ! -d "$MOD_TEST/runtime/lock" ]
[ ! -f "$MOD_TEST/runtime/active" ]
if [ "$SCENARIO" = offline_disable ]; then
    [ "$result" = 0 ]
    [ "$(cat restarts)" = 0 ]
    [ ! -f "$MOD_TEST/runtime/error" ]
elif [ "$SCENARIO" != insecure ] && [ "$SCENARIO" != restart_failure ] && [ "$SCENARIO" != listener_failure ]; then
    [ "$result" = 0 ]
    [ -f observed-active ]
    [ ! -f "$MOD_TEST/runtime/error" ]
    if [ "$SCENARIO" = daemon_reset ]; then [ "$(cat restarts)" = 3 ]; fi
    if [ "$SCENARIO" = reconnect ]; then [ "$(cat restarts)" = 2 ]; fi
else
    [ "$result" != 0 ]
    [ -f "$MOD_TEST/disable" ]
    [ -f "$MOD_TEST/runtime/error" ]
    if [ "$SCENARIO" = listener_failure ]; then [ "$(cat restarts)" = 5 ]; fi
fi
''', encoding='utf-8', newline='\n')
        subprocess.run([BASH, str(harness)], env={**os.environ, 'SCENARIO': scenario}, check=True)
        print(f'PASS: {scenario}, baseline restored')
