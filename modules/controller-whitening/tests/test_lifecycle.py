"""Exercise the actual shell lifecycle with fake OS/native boundaries.

No device, adb, root, ptrace or real process signals. Production state-machine
branches are unchanged; proc/process/sysprop/hash/sleep/native dependencies
are controlled to reach failures and races deterministically.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SHELL = os.environ.get('GXR_TEST_SHELL') or shutil.which('sh')
if os.name == 'nt':
    SHELL = os.environ.get('GXR_TEST_SHELL', 'C:/Program Files/Git/bin/bash.exe')

MOCKS = r'''
id() { echo 0; }
sync() { :; }
getprop() { cat "$MODDIR/boot"; }
pidof() { cat "$MODDIR/target.pid"; }
sha256sum() { [ ! -f "$MODDIR/bad-checksum" ]; }
start_of() {
 case "$1" in
  123) [ -f "$MODDIR/target.alive" ] && cat "$MODDIR/target.start" ;;
  456) echo 100 ;;
  "$$") echo 42 ;;
  *) [ -f "$MODDIR/owner.alive" ] && echo 77 ;;
 esac
}
readlink() {
 if [ -f "$MODDIR/wrong-exe" ]; then echo /unrelated/process; else echo "$BIN"; fi
}
query() {
 printf '%s %s\n' "$1" "$2" >> "$MODDIR/native.calls"
 if [ "$2" = preflight ] && [ -f "$MODDIR/health-error" ]; then echo ERROR; return 1; fi
 cat "$MODDIR/runtime"
}
kill() {
 printf '%s %s\n' "$1" "$2" >> "$MODDIR/signals"
 case "$1" in
  -TERM)
   if [ -f "$MODDIR/stopped-owner" ]; then touch "$MODDIR/pending-term";
   elif [ ! -f "$MODDIR/stuck-owner" ]; then
    rm -f "$MODDIR/owner.alive"
    [ -f "$MODDIR/restore-error" ] || echo 'STOCK pid=123 start=99' > "$MODDIR/runtime"
   fi ;;
  -CONT)
   if [ -f "$MODDIR/pending-term" ]; then
    rm -f "$MODDIR/owner.alive" "$MODDIR/stopped-owner"
    echo 'STOCK pid=123 start=99' > "$MODDIR/runtime"
   fi ;;
 esac
 return 0
}
tick=0
sleep() {
 tick=$((tick+1))
 command sleep 0.04
 SCENARIO_STEP
 [ "$tick" -lt 30 ] || { echo 'test exceeded dependency ticks'; exit 99; }
}
if [ "$MODE" = _run ]; then
 mkdir -p "$STATE/worker"
 printf '%s 42\n' "$$" > "$STATE/worker/pid"
fi
'''


class Lifecycle(unittest.TestCase):
    def run_case(self, scenario, mode='_run', lease=None, disabled=False):
        with tempfile.TemporaryDirectory(dir=ROOT/'tests') as temp:
            p = Path(temp)
            for name in ['bin', 'state', 'ram', 'proc/123', 'proc/456', 'proc/sys/kernel/random']:
                (p/name).mkdir(parents=True, exist_ok=True)
            (p/'proc/mounts').write_text('tmpfs /dev tmpfs rw 0 0\n')
            (p/'proc/sys/kernel/random/boot_id').write_text('boot-new\n')
            for pid in [123, 456]:
                (p/f'proc/{pid}/cmdline').write_bytes(b'/vendor/bin/hw/vendor.samsung.hardware.secxrcontroller@1.0-service\0')
            for name, value in {'boot':'1\n', 'target.pid':'123\n', 'target.start':'99\n',
                                'runtime':'STOCK pid=123 start=99\n'}.items():
                (p/name).write_text(value)
            (p/'target.alive').touch()
            (p/'owner.alive').touch()
            if disabled:
                (p/'disable').touch()
            if lease:
                (p/'state/active.lease').write_text(lease+'\n', newline='\n')
            steps = {
                'normal': '[ "$tick" -ne 3 ] || touch "$MODDIR/disable"',
                'service_restart': '[ "$tick" -ne 2 ] || echo 456 > "$MODDIR/target.pid"',
                'health_failure': '[ "$tick" -ne 2 ] || touch "$MODDIR/health-error"',
                'native_died': '''if [ "$tick" = 2 ]; then rm -f "$MODDIR/owner.alive"; echo 'STOCK pid=123 start=99' > "$MODDIR/runtime"; fi''',
                'boot_cancel': '[ "$tick" -ne 2 ] || touch "$MODDIR/disable"',
                'disable_during_launch': ':',
            }
            if scenario == 'external':
                (p/'runtime').write_text('WHITENING_ACTIVE pid=123 start=99\n')
            if scenario == 'checksum':
                (p/'bad-checksum').touch()
            if scenario == 'boot_cancel':
                (p/'boot').write_text('0\n')
            if scenario in ['stopped_owner', 'stuck_owner', 'restore_error', 'wrong_exe', 'reused_owner']:
                (p/'runtime').write_text('WHITENING_ACTIVE pid=123 start=99\n')
                marker = {'stopped_owner':'stopped-owner', 'stuck_owner':'stuck-owner',
                          'restore_error':'restore-error', 'wrong_exe':'wrong-exe'}.get(scenario)
                if marker:
                    (p/marker).touch()
            native = '''#!/bin/sh
dir=${0%/bin/*}
echo "$1 $2" >> "$dir/native.calls"
[ "$2" = hold ] || exit 91
echo hold > "$dir/hold.called"
echo 'WHITENING_ACTIVE pid=123 start=99' > "$dir/runtime"
echo 'MODE HOLD_UNTIL_USER_OFF; fixture'
'''
            if scenario == 'disable_during_launch':
                # Disable becomes visible exactly after the durable owner
                # lease is recorded; _run must stop before a health cycle.
                extra = '''
write_lease() {
 printf '%s %s %s %s %s %s %s\\n' "$BOOT" "$TP" "$TS" "$1" "$2" "$$" "$MY_START" > "$LEASE"
 if [ "$1" != 0 ]; then command sleep 0.08; touch "$MODDIR/disable"; fi
}
'''.replace('\\n', '\n')
            else:
                extra = ''
            (p/'bin/gxr-whitening').write_text(native, newline='\n')
            (p/'bin/gxr-whitening').chmod(0o755)
            source = (ROOT/'module/auto.sh').read_text()
            source = source.replace('STATE=/dev/gxr_controller_whitening', 'STATE=$MODDIR/ram')
            source = source.replace('/proc/', '$MODDIR/proc/')
            mocks = MOCKS.replace('SCENARIO_STEP', steps.get(scenario, ':')) + extra
            source = source.replace('if ! setup; then', mocks + '\nif ! setup; then')
            (p/'test.sh').write_text(source, newline='\n')
            result = subprocess.run([SHELL, './test.sh', mode], cwd=p, capture_output=True, text=True, timeout=15)
            def text(name):
                return (p/name).read_text() if (p/name).exists() else ''
            return result, {
                'disabled': (p/'disable').exists(), 'lease': (p/'state/active.lease').exists(),
                'hold': (p/'hold.called').exists(), 'signals': text('signals'),
                'calls': text('native.calls'), 'failure': text('state/failure'),
                'runtime': text('runtime'), 'launch': (p/'ram/launch').exists(),
            }

    def test_owned_clean_disable_restores_and_clears_lease(self):
        r, s = self.run_case('normal')
        self.assertEqual(r.returncode, 0, r.stdout+r.stderr)
        self.assertTrue(s['hold'] and s['disabled'])
        self.assertFalse(s['lease'] or s['launch'])
        self.assertTrue(s['runtime'].startswith('STOCK'))
        self.assertIn('-TERM ', s['signals'])

    def test_external_active_is_never_adopted_or_stopped(self):
        r, s = self.run_case('external')
        self.assertNotEqual(r.returncode, 0)
        self.assertTrue(s['disabled'])
        self.assertFalse(s['hold'] or s['lease'])
        self.assertEqual(s['signals'], '')
        self.assertNotIn(' off', s['calls'])
        self.assertIn('External whitening patch', s['failure'])

    def test_disabled_report_identifies_external_patch(self):
        r, s = self.run_case('external', mode='status', disabled=True)
        self.assertEqual(r.returncode, 0, r.stdout+r.stderr)
        self.assertIn('DISABLED - External or unowned', r.stdout)
        self.assertEqual(s['signals'], '')

    def test_disabled_resume_never_queries_or_launches(self):
        r, s = self.run_case('normal', mode='resume', disabled=True)
        self.assertEqual(r.returncode, 0, r.stdout+r.stderr)
        self.assertEqual(s['calls'], '')
        self.assertFalse(s['hold'])

    def test_reboot_interrupted_lease_quarantines_without_reapply(self):
        r, s = self.run_case('normal', mode='resume', lease='boot-old 123 99 777 77 888 42')
        self.assertNotEqual(r.returncode, 0)
        self.assertTrue(s['disabled'])
        self.assertFalse(s['hold'])
        self.assertEqual(s['signals'], '')
        self.assertIn('quarantined', s['failure'])

    def test_service_restart_health_error_owner_death_disable_without_retry(self):
        for scenario in ['service_restart', 'health_failure', 'native_died']:
            with self.subTest(scenario=scenario):
                r, s = self.run_case(scenario)
                self.assertNotEqual(r.returncode, 0, r.stdout+r.stderr)
                self.assertTrue(s['disabled'])
                self.assertEqual(s['calls'].count(' hold'), 1, s)
                self.assertFalse(s['lease'])

    def test_checksum_failure_and_boot_cancellation_never_launch(self):
        for scenario in ['checksum', 'boot_cancel']:
            with self.subTest(scenario=scenario):
                r, s = self.run_case(scenario)
                self.assertFalse(s['hold'] or s['lease'])
                self.assertEqual(s['signals'], '')
                self.assertTrue(s['disabled'])

    def test_disable_during_launch_restores_without_health_cycle(self):
        r, s = self.run_case('disable_during_launch')
        self.assertEqual(r.returncode, 0, r.stdout+r.stderr)
        self.assertTrue(s['disabled'] and s['hold'])
        self.assertFalse(s['lease'] or s['launch'])
        self.assertIn('-TERM ', s['signals'])
        self.assertEqual(s['calls'].count(' preflight'), 1)

    def test_stopped_owned_helper_gets_term_then_cont_never_target_signal(self):
        r, s = self.run_case('stopped_owner', mode='disable', lease='boot-new 123 99 777 77 888 42')
        self.assertEqual(r.returncode, 0, r.stdout+r.stderr)
        self.assertEqual(s['signals'].splitlines(), ['-TERM 777', '-CONT 777'])
        self.assertFalse(s['lease'])
        self.assertNotIn(' off', s['calls'])

    def test_stuck_owner_failed_restore_and_wrong_identity_remain_quarantined(self):
        for scenario, stamp in [('stuck_owner',77), ('restore_error',77), ('wrong_exe',77), ('reused_owner',88)]:
            with self.subTest(scenario=scenario):
                r, s = self.run_case(scenario, mode='disable', lease=f'boot-new 123 99 777 {stamp} 888 42')
                self.assertNotEqual(r.returncode, 0, r.stdout+r.stderr)
                self.assertTrue(s['disabled'] and s['lease'])
                self.assertNotIn(' off', s['calls'])
                if scenario in ['wrong_exe', 'reused_owner']:
                    self.assertEqual(s['signals'], '')


    def test_action_reports_only_and_explicit_commands_route_directly(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'tests') as temp:
            p = Path(temp)
            (p/'action.sh').write_text((ROOT/'module/action.sh').read_text(), newline='\n')
            (p/'auto.sh').write_text('#!/bin/sh\nprintf "%s\\n" "$1" >> "${0%/*}/calls"\necho "DISABLED - fixture"\n', newline='\n')
            for args, expected in [([], 'status'), (['on'], 'enable'), (['off'], 'disable'), (['status'], 'status')]:
                with self.subTest(args=args):
                    (p/'calls').write_text('')
                    result = subprocess.run([SHELL, './action.sh', *args], cwd=p, capture_output=True, text=True, timeout=5)
                    self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
                    self.assertEqual((p/'calls').read_text().splitlines(), [expected])
                    if not args:
                        self.assertIn('KernelSU 1.0 saves the checkbox preference', result.stdout)
                        self.assertIn('soft-reboot', result.stdout)
                        self.assertIn('Action checks status', result.stdout)
                        self.assertNotIn('Live control', result.stdout)


if __name__ == '__main__':
    unittest.main(verbosity=2)
