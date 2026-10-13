"""Host checks for the OTA control scripts. No device: Android commands are stubbed.

Runs the real module scripts (ota-ctl.sh, action.sh, uninstall.sh) under a
temporary module directory with stub `pm`, `settings`, `getprop` and `id`
programs in PATH, so the baseline, restore, boot, failure and removal paths are
exercised without touching a headset.
"""
import os
import pathlib
import shutil
import subprocess
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
PKG_A = 'com.wssyncmldm'
PKG_B = 'com.sec.android.soagent'
KEY = 'ota_disable_automatic_update'
SCRIPTS = ('ota-ctl.sh', 'action.sh', 'service.sh', 'uninstall.sh')
WORK = ROOT / 'dist/ota-control-tests'
WORK.mkdir(parents=True, exist_ok=True)
BASH = 'C:/Program Files/Git/bin/bash.exe' if os.name == 'nt' else shutil.which('bash')
assert os.path.exists(BASH) or shutil.which(BASH), BASH

PM_STUB = '''#!/bin/sh
DB=${PM_DB:?}
DENY=${PM_DENY:-}
[ -f "$DB" ] || : > "$DB"
case "$1" in
list)
 case "$*" in
  *-d*) sed 's/^/package:/' "$DB" ;;
 esac
 ;;
disable-user)
 for p in "$@"; do
  case "$p" in com.*|org.*) ;; *) continue ;; esac
  case " $DENY " in *" $p "*) continue ;; esac
  grep -qx "$p" "$DB" || echo "$p" >> "$DB"
 done
 ;;
enable)
 for p in "$@"; do
  case "$p" in com.*|org.*) ;; *) continue ;; esac
  grep -vx "$p" "$DB" > "$DB.tmp" 2>/dev/null || : > "$DB.tmp"
  mv "$DB.tmp" "$DB"
 done
 ;;
esac
exit 0
'''

SETTINGS_STUB = '''#!/bin/sh
DB=${SETTINGS_DB:?}
[ -f "$DB" ] || : > "$DB"
op=$1
key=$3
val=$4
case "$op" in
get)
 row=$(grep "^$key=" "$DB" 2>/dev/null | tail -n 1)
 if [ -n "$row" ]; then printf '%s\\n' "${row#*=}"; else printf 'null\\n'; fi
 ;;
put)
 grep -v "^$key=" "$DB" > "$DB.tmp" 2>/dev/null || : > "$DB.tmp"
 printf '%s=%s\\n' "$key" "$val" >> "$DB.tmp"
 mv "$DB.tmp" "$DB"
 ;;
delete)
 grep -v "^$key=" "$DB" > "$DB.tmp" 2>/dev/null || : > "$DB.tmp"
 mv "$DB.tmp" "$DB"
 ;;
esac
exit 0
'''

GETPROP_STUB = '''#!/bin/sh
if [ -n "${GETPROP_BOOT_FILE:-}" ] && [ -f "$GETPROP_BOOT_FILE" ]; then
 cat "$GETPROP_BOOT_FILE"
else
 printf '%s\\n' "${GETPROP_BOOT_COMPLETED:-1}"
fi
'''

# The host has no Android root, so the behaviour scenarios run copies of the
# scripts with the root guard neutralised. check_root_guard runs the pristine
# script and verifies the guard itself.
GUARD_OTA = '[ "$(id -u)" = 0 ] || { say \'DISABLED - Error: root is required.\'; exit 1; }'
GUARD_UNINSTALL = '[ "$(id -u)" = 0 ] || exit 0'


def msys(path):
    text = pathlib.Path(path).as_posix()
    if len(text) > 1 and text[1] == ':':
        return '/' + text[0].lower() + text[2:]
    return text


class Sandbox:
    def make_stubs(self, directory, trace=False):
        directory.mkdir()
        for name, text in (('pm', PM_STUB), ('settings', SETTINGS_STUB), ('getprop', GETPROP_STUB)):
            if trace:
                lines = text.split('\n')
                text = '\n'.join([lines[0], 'printf \'%s\\n\' "$0" >> "${TOOL_TRACE:?}"'] + lines[1:])
            stub = directory / name
            stub.write_text(text, newline='\n')
            stub.chmod(0o755)

    def __init__(self, root):
        self.root = pathlib.Path(root)
        self.tool_bin = self.root / 'tools-bin'
        self.make_stubs(self.root / 'bin')
        self.make_stubs(self.tool_bin, trace=True)
        (self.root / 'module').mkdir()
        (self.root / 'state').mkdir()
        guards = {'ota-ctl.sh': GUARD_OTA, 'uninstall.sh': GUARD_UNINSTALL}
        for name in SCRIPTS:
            text = (HERE / 'module' / name).read_text('utf-8')
            if name in guards:
                assert guards[name] in text, name
                text = text.replace(guards[name], ': # host fixture: no Android root')
            (self.root / 'module' / name).write_text(text, newline='\n')
        self.pm_db = self.root / 'pm-db'
        self.settings_db = self.root / 'settings-db'
        self.pm_db.write_text('', newline='\n')
        self.settings_db.write_text('', newline='\n')
        self.marker('disable')

    def marker(self, name):
        (self.root / 'module' / name).touch()

    def has_marker(self, name):
        return (self.root / 'module' / name).exists()

    def disabled(self):
        return [line for line in self.pm_db.read_text().split() if line]

    def setting(self):
        for line in self.settings_db.read_text().splitlines():
            if line.startswith(KEY + '='):
                return line.split('=', 1)[1]
        return 'null'

    def seed(self, disabled=(), setting=None):
        self.pm_db.write_text(''.join(f'{p}\n' for p in disabled), newline='\n')
        self.settings_db.write_text('' if setting is None else f'{KEY}={setting}\n', newline='\n')

    def record(self):
        state = self.root / 'state'
        return (state / 'state').read_text().strip() if (state / 'state').exists() else None

    def baseline(self):
        state = self.root / 'state' / 'baseline'
        return state.read_text().split() if state.exists() else []

    def failed(self):
        return (self.root / 'state' / 'failed').exists()

    def env(self, boot='1', deny='', tools=False, trace=None, boot_file=None):
        env = dict(os.environ)
        env['PATH'] = msys(self.root / 'bin') + ':' + env['PATH']
        env['PM_DB'] = msys(self.pm_db)
        env['SETTINGS_DB'] = msys(self.settings_db)
        env['GXR_OTA_STATE'] = msys(self.root / 'state')
        env['GETPROP_BOOT_COMPLETED'] = boot
        env['PM_DENY'] = deny
        if boot_file is not None:
            env['GETPROP_BOOT_FILE'] = boot_file
        if tools:
            env['GXR_OTA_PM'] = msys(self.tool_bin / 'pm')
            env['GXR_OTA_SETTINGS'] = msys(self.tool_bin / 'settings')
            env['GXR_OTA_GETPROP'] = msys(self.tool_bin / 'getprop')
        if trace is not None:
            env['TOOL_TRACE'] = msys(trace)
        return env

    def run(self, script, *args, boot='1', deny='', timeout=60, tools=False, trace=None):
        return subprocess.run([BASH, msys(self.root / 'module' / script), *args],
                               capture_output=True, text=True, timeout=timeout,
                               env=self.env(boot, deny, tools, trace))


def expect(result, code, text=None, absent=None):
    assert result.returncode == code, (result.returncode, result.stdout, result.stderr)
    if text is not None:
        assert text in result.stdout, (text, result.stdout)
    if absent is not None:
        assert absent not in result.stdout, (absent, result.stdout)


def check_block_and_restore():
    with tempfile.TemporaryDirectory(dir=WORK) as tmp:
        b = Sandbox(tmp)
        b.seed(disabled=[PKG_B], setting=None)
        expect(b.run('action.sh', 'status'), 0,
               'package com.wssyncmldm: enabled')
        expect(b.run('action.sh', 'status'), 0, 'ota_disable_automatic_update: null')
        expect(b.run('action.sh', 'status'), 0, 'GXR_UPDATE_STATE=STOCK')
        expect(b.run('action.sh', 'on'), 0)
        assert sorted(b.disabled()) == sorted([PKG_A, PKG_B]), b.disabled()
        assert b.setting() == '1', b.setting()
        assert b.record() == 'blocked', b.record()
        assert not b.has_marker('disable')
        assert b.baseline() == [f'package:{PKG_A}:enabled', f'package:{PKG_B}:disabled', 'setting:null'], b.baseline()
        expect(b.run('action.sh', 'status'), 0, 'package com.wssyncmldm: disabled')
        expect(b.run('action.sh', 'status'), 0, 'ota_disable_automatic_update: 1')
        expect(b.run('action.sh', 'status'), 0, absent='GXR_UPDATE_STATE=STOCK')
        expect(b.run('action.sh', 'on'), 0)
        assert sorted(b.disabled()) == sorted([PKG_A, PKG_B]), b.disabled()
        expect(b.run('action.sh', 'off'), 0)
        assert b.disabled() == [PKG_B], b.disabled()
        assert b.setting() == 'null', b.setting()
        assert b.record() == 'allowed', b.record()
        assert b.has_marker('disable')
        expect(b.run('action.sh', 'status'), 0, f'package {PKG_B}: disabled')
        expect(b.run('action.sh', 'status'), 0, 'GXR_UPDATE_STATE=STOCK')
        assert b.baseline() == [f'package:{PKG_A}:enabled', f'package:{PKG_B}:disabled', 'setting:null']
    print('PASS baseline, block, restore: vendor-disabled package stays disabled, unset setting returns to null, stock reported')


def check_preset_setting():
    with tempfile.TemporaryDirectory(dir=WORK) as tmp:
        b = Sandbox(tmp)
        b.seed(setting='1')
        expect(b.run('action.sh', 'on'), 0)
        assert b.baseline() == [f'package:{PKG_A}:enabled', f'package:{PKG_B}:enabled', 'setting:1'], b.baseline()
        expect(b.run('action.sh', 'off'), 0)
        assert b.setting() == '1', b.setting()
        expect(b.run('action.sh', 'status'), 0, 'GXR_UPDATE_STATE=STOCK')
    print('PASS baseline records a pre-set ota_disable_automatic_update and restores it as 1, not null')


def check_boot_reassert():
    with tempfile.TemporaryDirectory(dir=WORK) as tmp:
        b = Sandbox(tmp)
        b.seed()
        expect(b.run('action.sh', 'on'), 0)
        b.seed(disabled=[])
        expect(b.run('ota-ctl.sh', 'boot', boot='1'), 0)
        assert b.disabled() == [PKG_A, PKG_B], b.disabled()
        b.marker('disable')
        expect(b.run('ota-ctl.sh', 'boot', boot='1'), 0)
        assert b.disabled() == [], b.disabled()
        assert b.setting() == 'null', b.setting()
        assert b.record() == 'allowed', b.record()
        expect(b.run('action.sh', 'status'), 0, 'GXR_UPDATE_STATE=STOCK')
    print('PASS boot re-asserts the block while enabled and restores the baseline while disabled')


def check_boot_waits():
    # The killed boot stage can leave a short-lived leftover holding its log
    # file, so this scenario tolerates an incomplete cleanup.
    with tempfile.TemporaryDirectory(dir=WORK, ignore_cleanup_errors=True) as tmp:
        b = Sandbox(tmp)
        b.seed()
        (b.root / 'module' / 'disable').unlink()
        env = b.env(boot='0')
        log = (b.root / 'boot-wait.log').open('w')
        proc = subprocess.Popen([BASH, msys(b.root / 'module' / 'ota-ctl.sh'), 'boot'],
                                stdout=log, stderr=subprocess.STDOUT, env=env)
        timed_out = False
        try:
            proc.wait(timeout=6)
        except subprocess.TimeoutExpired:
            timed_out = True
            if os.name == 'nt':
                subprocess.run(['taskkill', '/F', '/T', '/PID', str(proc.pid)], capture_output=True)
            else:
                proc.kill()
            proc.wait(timeout=15)
        log.close()
        assert timed_out, 'boot stage returned before sys.boot_completed'
        assert b.disabled() == [], b.disabled()
        assert b.setting() == 'null', b.setting()
    print('PASS boot stage changes nothing before sys.boot_completed')


def check_failure_latch():
    with tempfile.TemporaryDirectory(dir=WORK) as tmp:
        b = Sandbox(tmp)
        b.seed()
        expect(b.run('action.sh', 'on', deny=PKG_A), 1, 'DISABLED - Error: could not disable')
        assert b.has_marker('disable')
        assert b.failed()
        assert b.record() != 'blocked', b.record()
        assert PKG_A not in b.disabled()
        expect(b.run('action.sh', 'status'), 0, 'DISABLED - Error details:')
        expect(b.run('action.sh', 'status'), 0, absent='GXR_UPDATE_STATE=STOCK')
        expect(b.run('uninstall.sh'), 0)
        assert b.disabled() == [], b.disabled()
        assert b.setting() == 'null', b.setting()
    print('PASS failure latches: disable marker, failed record, no stock claim, and removal still reverts a half-applied block')


def check_removal_refusal():
    with tempfile.TemporaryDirectory(dir=WORK) as tmp:
        b = Sandbox(tmp)
        b.seed()
        b.marker('remove')
        expect(b.run('action.sh', 'on'), 1, 'removal was requested')
        assert b.disabled() == [], b.disabled()
        assert b.setting() == 'null', b.setting()
        expect(b.run('ota-ctl.sh', 'boot'), 0)
        assert b.disabled() == [], b.disabled()
    print('PASS removal request refuses to block and stops the boot stage')


def check_uninstall_restore():
    with tempfile.TemporaryDirectory(dir=WORK) as tmp:
        b = Sandbox(tmp)
        b.seed(disabled=[PKG_B])
        expect(b.run('action.sh', 'on'), 0)
        assert sorted(b.disabled()) == sorted([PKG_A, PKG_B])
        expect(b.run('uninstall.sh'), 0)
        assert b.disabled() == [PKG_B], b.disabled()
        assert b.setting() == 'null', b.setting()
        assert b.record() == 'allowed', b.record()
    print('PASS uninstall restores the baseline before the module directory goes away')


def check_tool_paths():
    with tempfile.TemporaryDirectory(dir=WORK) as tmp:
        b = Sandbox(tmp)
        trace = b.root / 'trace'
        expect(b.run('action.sh', 'status'), 0, 'ota_disable_automatic_update: null')
        assert not trace.exists(), 'the default run must resolve the tools from PATH'
        expect(b.run('action.sh', 'status', tools=True, trace=trace), 0, 'ota_disable_automatic_update: null')
        seen = {pathlib.Path(p).name for p in trace.read_text().split()}
        assert {'pm', 'settings'} <= seen, seen
        assert all('tools-bin' in p for p in trace.read_text().split()), trace.read_text()
        trace.write_text('', newline='\n')
        expect(b.run('action.sh', 'on', tools=True, trace=trace), 0)
        assert sorted(b.disabled()) == sorted([PKG_A, PKG_B]), b.disabled()
        assert b.setting() == '1', b.setting()
        seen = {pathlib.Path(p).name for p in trace.read_text().split()}
        assert 'pm' in seen and 'settings' in seen, seen
    print('PASS tool resolution: the absolute-path branch runs pm and settings and can block updates')


def check_service_entry():
    with tempfile.TemporaryDirectory(dir=WORK) as tmp:
        b = Sandbox(tmp)
        b.seed()
        (b.root / 'module' / 'disable').unlink()
        start = time.monotonic()
        expect(b.run('service.sh'), 0)
        elapsed = time.monotonic() - start
        assert elapsed < 5, elapsed
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline and b.record() != 'blocked':
            time.sleep(0.5)
        assert sorted(b.disabled()) == sorted([PKG_A, PKG_B]), b.disabled()
        assert b.setting() == '1', b.setting()
        assert b.record() == 'blocked', b.record()
    print('PASS service.sh: the KernelSU service stage returns at once and the detached boot work blocks updates')


def check_status_guidance():
    with tempfile.TemporaryDirectory(dir=WORK) as tmp:
        b = Sandbox(tmp)
        b.seed(disabled=[PKG_B])
        expect(b.run('action.sh', 'on'), 0)
        b.marker('disable')
        expect(b.run('action.sh', 'status'), 0, 'DISABLED - KernelSU preference OFF but the block is still live.')
        expect(b.run('action.sh', 'status'), 0, 'Restore now: sh ')
        expect(b.run('action.sh', 'status'), 0, absent='GXR_UPDATE_STATE=STOCK')
        expect(b.run('action.sh', 'off'), 0)
        expect(b.run('action.sh', 'status'), 0, 'DISABLED - KernelSU preference OFF; nothing from this module is applied.')
        expect(b.run('action.sh', 'status'), 0, 'GXR_UPDATE_STATE=STOCK')
    print('PASS status guidance: an off preference with a live block names the restore command and reports clean afterwards')


def check_boot_marker_race():
    with tempfile.TemporaryDirectory(dir=WORK) as tmp:
        b = Sandbox(tmp)
        b.seed()
        (b.root / 'module' / 'disable').unlink()
        boot_file = b.root / 'boot-completed'
        boot_file.write_text('0\n', newline='\n')
        log = (b.root / 'boot-race.log').open('w')
        proc = subprocess.Popen([BASH, msys(b.root / 'module' / 'ota-ctl.sh'), 'boot'],
                                stdout=log, stderr=subprocess.STDOUT,
                                env=b.env(boot_file=msys(boot_file)))
        time.sleep(4)
        b.marker('disable')
        boot_file.write_text('1\n', newline='\n')
        proc.wait(timeout=30)
        log.close()
        assert proc.returncode == 0, proc.returncode
        assert b.disabled() == [], b.disabled()
        assert b.setting() == 'null', b.setting()
        assert b.record() != 'blocked', b.record()
    print('PASS boot race: a module turned off while the boot stage waits is not blocked afterwards')


def check_root_guard():
    with tempfile.TemporaryDirectory(dir=WORK) as tmp:
        env = dict(os.environ)
        env['GXR_OTA_STATE'] = msys(tmp)
        result = subprocess.run([BASH, msys(HERE / 'module/ota-ctl.sh'), 'status'],
                                capture_output=True, text=True, env=env, timeout=60)
        assert result.returncode == 1, (result.returncode, result.stdout, result.stderr)
        assert 'DISABLED - Error: root is required.' in result.stdout, result.stdout
    print('PASS root guard: the pristine script refuses to run without root')


for check in (check_root_guard, check_tool_paths, check_service_entry, check_status_guidance,
              check_boot_marker_race, check_block_and_restore, check_preset_setting,
              check_boot_reassert, check_boot_waits, check_failure_latch, check_removal_refusal,
              check_uninstall_restore):
    check()
print('PASS OTA control: 12 host scenarios')
