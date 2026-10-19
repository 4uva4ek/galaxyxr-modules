"""Exercise production workers/actions with fake Android state; no headset I/O."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SHELL = os.environ.get('GXR_TEST_SHELL') or shutil.which('sh') or 'C:/Program Files/Git/bin/bash.exe'


def execute(work, source):
    (work/'test.sh').write_text(source.replace('/proc/', 'proc/'), newline='\n')
    return subprocess.run([SHELL, './test.sh'], cwd=work, capture_output=True, text=True, timeout=15)


class RestartLifecycle(unittest.TestCase):
    def worker(self, module, scenario):
        with tempfile.TemporaryDirectory() as temp:
            work = Path(temp)
            haptic = module == 'haptic'
            worker, run = ('auto.worker', 'auto.enabled') if haptic else ('worker', 'run')
            (work/f'state/{worker}').mkdir(parents=True)
            (work/f'state/{run}').touch()
            (work/'autostart').mkdir()
            (work/'autostart/enabled').touch()
            for pid, start in [('123', '99'), ('456', '111')]:
                (work/f'proc/{pid}').mkdir(parents=True)
                (work/f'proc/{pid}/stat').write_text(pid+' (test) '+'0 '*19+start+'\n')
                (work/f'proc/{pid}/maps').write_text('/system/lib64/test.so\n')
            (work/'proc/mounts').write_text('tmpfs /dev tmpfs rw 0 0\n')
            (work/'proc/uptime').write_text('100.0 0.0\n')
            for name, value in [('boot', '1'), ('pid', '123'), ('targets', '123 99 test.app'), ('discovery', '0')]:
                (work/name).write_text(value+'\n')
            common = '''MODDIR=.
STATE=state
PACKAGE=test.app
LIBRARY=/system/lib64/test.so
SYSTEM_SERVICE=1
SOFT_RESTART_AWARE=1
id() { echo 0; }
getprop() { cat boot; }
sync() { :; }
sha256sum() { return 0; }
pidof() { cat pid; }
native() {
  if [ "${2:-}" = identity ]; then
    [ -r "proc/$1/stat" ] || return 4
    actual=$(sed 's/.*) //' "proc/$1/stat" | awk '{print $20}')
    [ "$actual" = "$3" ] || return 4
    return 0
  fi
  if [ "$1" = list ]; then
    [ "$(cat boot)" = 1 ] || { touch queried_during_restart; return 3; }
    result=$(cat discovery)
    [ "$result" = 0 ] || return "$result"
    cat targets; return 0
  fi
  if [ -f race_health ]; then
    rm race_health; echo 0 > boot; touch state/soft-restart; return 1
  fi
  if [ -f health_exit ]; then
    rm -f health_exit proc/123/stat
    echo '456 111 test.app' > targets
    return 4
  fi
  [ ! -f health_failure ] || return 1
  echo 'CORRECTED pid=123'; return 0
}
sh() {
  if [ "$2" = _auto-off ]; then
    echo restored >> restores
    [ ! -f restore_failure ]; return $?
  fi
  if [ "$2" = status ] || [ "$2" = _auto-check ]; then echo "MAIN pid=$(cat pid)"; return 0; fi
  [ "$2" = _auto-on ] || return 90
  echo "$3" >> attempts
  [ ! -f action_unavailable ] || return 3
  [ ! -f action_gone ] || return 4
  touch state/restore-needed
  [ ! -f activation_failure ] || return 1
  echo "$3" >> calls
}
tick=0
sleep() {
  tick=$((tick+1))
  if [ "$tick" = 1 ] && [ -f auto.pending ] && [ -f calls ]; then touch stale_pending; fi
  STEPS
  [ "$tick" -ne 8 ] || echo '140.0 0.0' > proc/uptime
  [ "$tick" -lt LIMIT ] || rm -f state/RUN
}
set -- _run
'''
            steps = ':'
            if scenario in ('planned', 'unannounced', 'retained'):
                steps = '''if [ "$tick" = 2 ]; then echo 0 > boot; NOTICE; fi
if [ "$tick" = 3 ]; then CHANGE; fi
if [ "$tick" = 4 ]; then echo 1 > boot; fi'''
                steps = steps.replace('NOTICE', ':' if scenario == 'unannounced' else 'touch state/soft-restart')
                steps = steps.replace('CHANGE', ':' if scenario == 'retained' else
                    "rm -f proc/123/stat; echo 456 > pid; echo '456 111 test.app' > targets")
            elif scenario == 'crash':
                steps = '[ "$tick" -ne 2 ] || { rm -f proc/123/stat; : > pid; : > targets; }'
            elif scenario == 'app_relaunch':
                steps = '''[ "$tick" -ne 2 ] || { rm -f proc/123/stat; : > targets; }
[ "$tick" -ne 3 ] || echo '456 111 test.app' > targets'''
            elif scenario == 'pid_reuse':
                steps = '''if [ "$tick" = 2 ]; then
  echo '123 (test) 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 112' > proc/123/stat
  echo '123 112 test.app' > targets
fi'''
            elif scenario == 'action_gone':
                (work/'action_gone').touch()
                steps = '[ "$tick" -ne 3 ] || rm action_gone'
            elif scenario == 'health_exit':
                (work/'health_exit').touch()
            elif scenario == 'health_failure':
                (work/'health_failure').touch()
            elif scenario == 'lost_eligibility':
                steps = '[ "$tick" -ne 2 ] || : > targets'
            elif scenario == 'pm_flaky':
                steps = '''[ "$tick" -ne 2 ] || echo 3 > discovery
[ "$tick" -ne 5 ] || echo 0 > discovery'''
            elif scenario == 'pm_dead':
                (work/'discovery').write_text('3\n')
            elif scenario == 'action_flaky':
                (work/'action_unavailable').touch()
                steps = '[ "$tick" -ne 3 ] || rm action_unavailable'
            elif scenario == 'action_dead':
                (work/'action_unavailable').touch()
            elif scenario == 'activation_failure':
                (work/'activation_failure').touch()
            elif scenario == 'restore_failure':
                (work/'restore_failure').touch()
            elif scenario == 'health_restart_race':
                (work/'race_health').touch()
                steps = '''if [ "$tick" -ge 9 ]; then
  rm -f proc/123/stat; echo 456 > pid; echo '456 111 test.app' > targets; echo 1 > boot
fi'''
            code = common.replace('STEPS', steps).replace('RUN', run).replace('LIMIT', '40' if scenario.endswith('dead') else '12')
            if haptic:
                # system_server keeps running in these scenarios (pid 1 here).
                (work/'proc/1').mkdir()
                (work/'proc/1/stat').write_text('1 (system_server) '+'0 '*19+'5\n', newline='\n')
                code = code.replace('pidof() { cat pid; }',
                                    'pidof() { if [ "$1" = system_server ]; then echo 1; else cat pid; fi; }')
                code += (ROOT/'modules/haptic-main/module/autostart.sh').read_text()
            else:
                code += (ROOT/'modules/openxr-velocity-frame/module/auto.sh').read_text().replace(
                    'MODDIR=${0%/*}', 'MODDIR=.').replace('STATE=/dev/gxr_openxr_velocity_frame', 'STATE=state').replace(
                    '"$MODDIR/bin/gxr-openxr-velocity"', 'native')
            result = execute(work, code)
            if not haptic and scenario != 'activation_failure':
                self.assertFalse((work/'stale_pending').exists(), result.stdout+result.stderr)
            self.assertFalse((work/'queried_during_restart').exists(), result.stdout+result.stderr)
            self.assertFalse((work/f'state/{worker}').exists(), result.stdout+result.stderr)
            calls = (work/'calls').read_text().splitlines() if (work/'calls').exists() else []
            return result, calls, (work/'disable').exists(), (work/'restores').exists()

    def test_planned_restart_replaces_process_without_disabling(self):
        for module in ('haptic', 'velocity'):
            with self.subTest(module=module):
                result, calls, disabled, _ = self.worker(module, 'planned')
                self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
                self.assertEqual(calls, ['123:99', '456:111'])
                self.assertFalse(disabled)

    def test_planned_restart_retains_surviving_process(self):
        for module in ('haptic', 'velocity'):
            result, calls, disabled, _ = self.worker(module, 'retained')
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            self.assertEqual(calls, ['123:99'])
            self.assertFalse(disabled)

    def test_service_exit_and_actual_activation_failure_still_block_retry(self):
        for module in ('haptic', 'velocity'):
            for scenario in (('unannounced', 'crash', 'activation_failure') if module == 'haptic' else ('activation_failure',)):
                with self.subTest(module=module, scenario=scenario):
                    result, calls, disabled, _ = self.worker(module, scenario)
                    self.assertNotEqual(result.returncode, 0, result.stdout+result.stderr)
                    self.assertTrue(disabled)
                    self.assertLessEqual(len(calls), 1)

    def test_velocity_app_exit_and_reuse_do_not_disable(self):
        cases = {
            'crash': ['123:99'],
            'unannounced': ['123:99', '456:111'],
            'app_relaunch': ['123:99', '456:111'],
            'pid_reuse': ['123:99', '123:112'],
            'action_gone': ['123:99'],
            'health_exit': ['123:99', '456:111'],
        }
        for scenario, expected in cases.items():
            with self.subTest(scenario=scenario):
                result, calls, disabled, _ = self.worker('velocity', scenario)
                self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
                self.assertEqual(calls, expected)
                self.assertFalse(disabled)

    def test_live_health_failure_still_disables(self):
        result, calls, disabled, restored = self.worker('velocity', 'health_failure')
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(disabled and restored)
        self.assertEqual(calls, ['123:99'])

    def test_package_manager_recovery(self):
        for scenario in ('pm_flaky', 'action_flaky'):
            result, calls, disabled, _ = self.worker('velocity', scenario)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            self.assertEqual(calls, ['123:99'])
            self.assertFalse(disabled)

    def test_restart_during_read_only_health_check(self):
        result, calls, disabled, _ = self.worker('velocity', 'health_restart_race')
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        self.assertEqual(calls, ['123:99', '456:111'])
        self.assertFalse(disabled)

    def test_package_manager_wait_is_bounded_and_does_not_fake_restore_failure(self):
        for scenario in ('pm_dead', 'action_dead'):
            result, calls, disabled, restored = self.worker('velocity', scenario)
            self.assertNotEqual(result.returncode, 0, result.stdout+result.stderr)
            self.assertEqual(calls, [])
            self.assertTrue(disabled)
            self.assertFalse(restored)

    def test_still_live_process_is_not_forgotten(self):
        result, _, disabled, restored = self.worker('velocity', 'lost_eligibility')
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(disabled)
        self.assertTrue(restored)

    def test_failed_restore_is_reported_and_disables(self):
        result, _, disabled, restored = self.worker('velocity', 'restore_failure')
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(disabled and restored)
        self.assertIn('Restore failed', result.stdout)

    def test_actions_defer_only_before_writes(self):
        for module, binary, state in [('haptic-main', 'gxr-main', 'gxr_haptic_main'),
                                      ('openxr-velocity-frame', 'gxr-openxr-velocity', 'gxr_openxr_velocity_frame')]:
            scenarios = ['boot_wait', 'failed_write']
            if module == 'haptic-main': scenarios += ['service_wait', 'identity_wait', 'library_wait']
            if module == 'openxr-velocity-frame': scenarios += ['discovery_wait', 'native_wait', 'target_gone', 'native_gone', 'live_missing', 'identity_error']
            for scenario in scenarios:
                with self.subTest(module=module, scenario=scenario), tempfile.TemporaryDirectory() as temp:
                    work = Path(temp)
                    for directory in ['state/worker', 'state/auto.worker', 'proc/123']:
                        (work/directory).mkdir(parents=True)
                    for name in ['state/run', 'state/auto.enabled', 'auto.sh']:
                        (work/name).touch()
                    (work/'proc/mounts').write_text('tmpfs /dev tmpfs rw 0 0\n')
                    (work/'proc/123/stat').write_text('123 (test) '+'0 '*19+'99\n')
                    (work/'proc/123/maps').write_text('/system/lib64/libsecxrcontrollerservice_jni.xrcontroller.samsung.so\n')
                    if scenario == 'library_wait': (work/'proc/123/maps').write_text('')
                    if scenario == 'identity_wait': (work/'proc/123/stat').write_text('123 (test) '+'0 '*19+'100\n')
                    code = '''id() { echo 0; }
getprop() { echo BOOT; }
pidof() { echo 123; }
sha256sum() {
  case "$1" in
    -c) return 0 ;;
    */secxrcontroller.apk) echo 8130b5542c36235eb1e951b9245684299d6a7ddd1a8c95f231e8241e477673cb ;;
    */libsecxrcontrollerservice_jni.xrcontroller.samsung.so) echo 80d45131558b28107cf7e3c9a2c483bce4e2207b934e4f25e951f0fc79d1cd36 ;;
  esac
}
native() {
  if [ "${2:-}" = identity ]; then return IDENTITY; fi
  if [ "$1" = list ]; then
    [ DISCOVERY = 0 ] || return 3
    [ LISTED = 1 ] || return 0
    echo '123 99 test.app'; return 0
  fi
  touch native_called
  return NATIVE
}
set -- _auto-on 123:99
'''.replace('BOOT', '0' if scenario == 'boot_wait' else '1').replace(
                        'DISCOVERY', '3' if scenario == 'discovery_wait' else '0').replace(
                        'NATIVE', '3' if scenario == 'native_wait' else '4' if scenario == 'native_gone' else '1').replace(
                        'IDENTITY', '4' if scenario == 'target_gone' else '1' if scenario == 'identity_error' else '0').replace(
                        'LISTED', '0' if scenario in ('target_gone', 'live_missing') else '1')
                    code += (ROOT/'modules'/module/'module/action.sh').read_text().replace(
                        f'/dev/{state}', 'state').replace('/dev/gxr_openxr_transaction', 'global-lock').replace(
                        f'"$MODDIR/bin/{binary}"', 'native')
                    if scenario == 'service_wait': code=code.replace('pidof() { echo 123; }', 'pidof() { :; }')
                    result = execute(work, code)
                    waiting = scenario not in ('failed_write', 'live_missing', 'identity_error')
                    expected = 4 if scenario in ('target_gone', 'native_gone') else 3 if waiting else 1
                    self.assertEqual(result.returncode, expected, result.stdout+result.stderr)
                    self.assertEqual((work/'disable').exists(), not waiting)
                    self.assertEqual((work/'native_called').exists(), scenario in ('native_wait', 'native_gone', 'failed_write'))
                    if module == 'openxr-velocity-frame':
                        self.assertEqual((work/'state/restore-needed').exists(), scenario == 'failed_write')

    def test_standard_notice_cannot_start_or_recover_worker(self):
        for running, disabled in [(False, False), (True, False), (True, True)]:
            with self.subTest(running=running, disabled=disabled), tempfile.TemporaryDirectory() as temp:
                work = Path(temp)
                (work/'proc').mkdir()
                (work/'proc/mounts').write_text('tmpfs /dev tmpfs rw 0 0\n')
                (work/'module.prop').write_text('id=gxr_haptic_main\n')
                (work/'autostart').mkdir()
                (work/'autostart/pending').touch()
                if running:
                    (work/'state/auto.worker').mkdir(parents=True)
                    (work/'state/auto.enabled').touch()
                if disabled: (work/'disable').touch()
                code = (ROOT/'tools/emulated-soft-reboot.sh').read_text().replace('STATE=/dev/$MODULE_ID', 'STATE=state')
                result = execute(work, code)
                self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
                self.assertEqual((work/'state/soft-restart').exists(), running and not disabled)
                self.assertTrue((work/'autostart/pending').exists())
                self.assertEqual((work/'disable').exists(), disabled)


if __name__ == '__main__':
    unittest.main()
