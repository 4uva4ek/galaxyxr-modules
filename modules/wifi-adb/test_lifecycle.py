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

for scenario in ('success', 'insecure', 'restart_failure'):
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
getprop() {
    case "$1" in
    service.adb.tcp.port) cat "$TEST_ROOT/port" ;;
    init.svc.adbd) echo running ;;
    ro.adb.secure) [ "$SCENARIO" = insecure ] && echo 0 || echo 1 ;;
    esac
}
setprop() {
    if [ "$1" = service.adb.tcp.port ]; then
        echo "$2" > "$TEST_ROOT/port"
    elif [ "$SCENARIO" = restart_failure ] && [ "$(cat "$TEST_ROOT/port")" = 5555 ]; then
        return 1
    fi
}
cat() {
    if [ "$1" = /proc/sys/kernel/random/boot_id ]; then echo test-boot; else command cat "$@"; fi
}
awk() { [ "$(cat "$TEST_ROOT/port")" = 5555 ]; }
sleep() {
    if [ -f "$MOD_TEST/runtime/active" ]; then
        touch "$TEST_ROOT/observed-active" "$MOD_TEST/disable"
    fi
}
export -f getprop setprop cat awk sleep
bash "$MOD_TEST/adb-ctl.sh" run && result=0 || result=$?
[ "$(cat port)" = 1234 ]
[ ! -d "$MOD_TEST/runtime/lock" ]
[ ! -f "$MOD_TEST/runtime/active" ]
if [ "$SCENARIO" = success ]; then
    [ "$result" = 0 ]
    [ -f observed-active ]
    [ ! -f "$MOD_TEST/runtime/error" ]
else
    [ "$result" != 0 ]
    [ -f "$MOD_TEST/disable" ]
    [ -f "$MOD_TEST/runtime/error" ]
fi
''', encoding='utf-8', newline='\n')
        subprocess.run([BASH, str(harness)], env={**os.environ, 'SCENARIO': scenario}, check=True)
        print(f'PASS: {scenario}, baseline restored')
