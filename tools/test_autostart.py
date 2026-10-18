"""Host-side lifecycle tests with fake Android processes; never touches a device."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SHELL = os.environ.get('GXR_TEST_SHELL') or shutil.which('sh')
if not SHELL and os.name == 'nt':
    SHELL = 'C:/Program Files/Git/bin/bash.exe'


class Autostart(unittest.TestCase):
    def run_worker(self, scenario, system=0, status="MAIN pid=123"):
        with tempfile.TemporaryDirectory() as temp:
            work = Path(temp)
            # Cold-reset boundary is covered with all six real entries in test_cold_boot_reset.py.
            (work/'cold-boot-reset.sh').write_text('exit 0\n', newline='\n')
            (work/'state/auto.worker').mkdir(parents=True)
            (work/'state/auto.enabled').touch()
            (work/'autostart').mkdir()
            (work/'autostart/enabled').touch()
            (work/'proc/123').mkdir(parents=True)
            (work/'proc/mounts').write_text('tmpfs /dev tmpfs rw 0 0\n')
            (work/'proc/uptime').write_text('100.00 0.00\n')
            (work/'proc/123/stat').write_text('123 (app with spaces) ' + '0 '*19 + '99\n')
            (work/'proc/123/maps').write_text('/system/lib64/test.so\n')
            (work/'pid').write_text('123\n')
            script = '''#!/bin/sh
MODDIR=.
STATE=state
PACKAGE=test.app
LIBRARY=/system/lib64/test.so
SYSTEM_SERVICE=SYSTEM_VALUE
id() { echo 0; }
getprop() { echo 1; }
sync() { :; }
sha256sum() { return 0; }
pidof() { cat pid; }
sh() {
  if [ "$2" = _auto-off ]; then echo restored > restored; return 0; fi
  if [ "$2" = status ]; then echo "MAIN pid=123"; return 0; fi
  [ "$2" = _auto-on ] && [ "$3" = 123:99 ] || return 90
  echo "$3" >> calls
  ACTION_RESULT
}
tick=0
sleep() {
  tick=$((tick + 1))
  SCENARIO
  [ "$tick" -lt 12 ] || rm -f state/auto.enabled
}
set -- _run
'''.replace('SYSTEM_VALUE', str(system)).replace('ACTION_RESULT',
                    'return 1' if scenario == 'failure' else 'return 0')
            script = script.replace('MAIN pid=123', status)
            steps = {
                'busy': ':',
                'once': ':',
                'failure': ':',
                'service_restart': '[ "$tick" -ne 2 ] || echo 456 > pid',
                'crash': '[ "$tick" -ne 2 ] || : > pid',
                'app_restart': '''if [ "$tick" = 2 ]; then echo '140.0 0.0' > proc/uptime; : > pid; fi
if [ "$tick" = 3 ]; then echo 123 > pid; fi''',
                'disabled': '[ "$tick" -ne 1 ] || touch disable',
                'not_ready': ':',
                'healthy': '[ "$tick" -ne 2 ] || echo "140.0 0.0" > proc/uptime',
            }
            if scenario == 'busy':
                script = script.replace('  return 0\n}\ntick=0',
                    '  [ "$(wc -l < calls)" -gt 1 ] || return 2\n  return 0\n}\ntick=0')
            if scenario == 'not_ready':
                (work/'proc/123/maps').write_text('')
            script = script.replace('SCENARIO', steps[scenario])
            script += (ROOT/'tools/autostart.sh').read_text().replace('/proc/', 'proc/')
            (work/'test.sh').write_text(script, newline='\n')
            result = subprocess.run([SHELL, 'test.sh'], cwd=work, capture_output=True, text=True, timeout=10)
            calls = (work/'calls').read_text().splitlines() if (work/'calls').exists() else []
            self.assertFalse((work/'state/auto.enabled').exists(), result.stdout + result.stderr)
            self.assertFalse((work/'state/auto.worker').exists(), result.stdout + result.stderr)
            if result.returncode != 0:
                self.assertFalse((work/'autostart/enabled').exists())
            if scenario == 'healthy':
                self.assertFalse((work/'autostart/pending').exists())
            return result, calls

    def test_success_clears_pending_after_observation(self):
        result, calls = self.run_worker('healthy')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(calls), 1)

    def test_haptic_tuning_status_survives_health_check(self):
        result, calls = self.run_worker('healthy', system=1,
            status='MAIN pid=123 site=123400 profile=step2-amplitude-stock')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(len(calls), 1)

    def test_resume_respects_checkbox_and_blocks_unfinished_attempt(self):
        for enabled, pending in [(False, False), (True, True), (True, False)]:
            with self.subTest(enabled=enabled, pending=pending), tempfile.TemporaryDirectory() as temp:
                work = Path(temp)
                # Cold-reset boundary is covered with all six real entries in test_cold_boot_reset.py.
                (work/'cold-boot-reset.sh').write_text('exit 0\n', newline='\n')
                (work/'proc').mkdir()
                (work/'proc/mounts').write_text('tmpfs /dev tmpfs rw 0 0\n')
                (work/'autostart').mkdir()
                if not enabled:
                    (work/'disable').touch()
                if pending:
                    (work/'autostart/pending').touch()
                script = '''MODDIR=.
STATE=state
id() { echo 0; }
sync() { :; }
nohup() { touch launched; }
set -- resume
'''
                script += (ROOT/'tools/autostart.sh').read_text().replace('/proc/', 'proc/')
                script += '\nwait\n'
                (work/'test.sh').write_text(script, newline='\n')
                result = subprocess.run([SHELL, 'test.sh'], cwd=work, capture_output=True, text=True, timeout=10)
                self.assertEqual((work/'launched').exists(), enabled and not pending)
                if pending:
                    self.assertNotEqual(result.returncode, 0)
                    self.assertFalse((work/'autostart/enabled').exists())
                    self.assertTrue((work/'autostart/failed').exists())
                else:
                    self.assertEqual(result.returncode, 0, result.stderr)

    def test_auto_package_initializes_once_and_respects_disable(self):
        with tempfile.TemporaryDirectory() as temp:
            work = Path(temp)
            # Cold-reset boundary is covered with all six real entries in test_cold_boot_reset.py.
            (work/'cold-boot-reset.sh').write_text('exit 0\n', newline='\n')
            (work/'proc').mkdir()
            (work/'proc/mounts').write_text('tmpfs /dev tmpfs rw 0 0\n')
            (work/'auto-default').touch()
            script = '''MODDIR=.
STATE=state
id() { echo 0; }
sync() { :; }
nohup() { touch launched; }
set -- resume
'''
            script += (ROOT/'tools/autostart.sh').read_text().replace('/proc/', 'proc/')
            script += '\nwait\n'
            (work/'test.sh').write_text(script, newline='\n')
            for first in (True, False):
                result = subprocess.run([SHELL, 'test.sh'], cwd=work, capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual((work/'launched').exists(), first)
                self.assertEqual((work/'autostart/enabled').exists(), first)
                if first:
                    (work/'launched').unlink()
                    (work/'autostart/enabled').unlink()
                    (work/'disable').touch()
                    (work/'state/auto.enabled').unlink()
                    (work/'state/auto.worker').rmdir()

    def test_patch_once_per_process(self):
        result, calls = self.run_worker('once')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(calls), 1)

    def test_busy_status_reader_retries_without_failure_latch(self):
        result, calls = self.run_worker('busy')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(len(calls), 2)

    def test_failed_activation_stops(self):
        result, calls = self.run_worker('failure')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(len(calls), 1)

    def test_system_service_restart_stops(self):
        result, calls = self.run_worker('service_restart', system=1)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(len(calls), 1)

    def test_early_app_exit_stops(self):
        result, calls = self.run_worker('crash')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(len(calls), 1)

    def test_app_can_restart_after_stable_interval(self):
        result, calls = self.run_worker('app_restart')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(calls), 2)

    def test_disabled_module_never_patched(self):
        result, calls = self.run_worker('disabled')
        self.assertEqual(result.returncode, 0)
        self.assertEqual(calls, [])

    def test_missing_library_never_patched(self):
        result, calls = self.run_worker('not_ready')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(calls, [])

    def test_manual_override_and_automatic_identity_guards(self):
        for module, binary, state in [('haptic-main', 'gxr-main', 'gxr_haptic_main'),
                                      ('controller-extrapolation', 'gxr-extrapolation', 'gxr_controller_extrapolation')]:
            for mode, enabled, identity, allowed in [('off', True, '', True),
                                                      ('_auto-on', False, '123:99', False),
                                                      ('_auto-on', True, '123:98', False),
                                                      ('_auto-on', True, '123:99', True)]:
                with self.subTest(module=module, mode=mode, enabled=enabled, identity=identity), tempfile.TemporaryDirectory() as temp:
                    work = Path(temp)
                    # Cold-reset boundary is covered with all six real entries in test_cold_boot_reset.py.
                    (work/'cold-boot-reset.sh').write_text('exit 0\n', newline='\n')
                    (work/'stream-clients.sh').write_bytes((ROOT/'tools/stream-clients.sh').read_bytes())
                    (work/'state/auto.worker').mkdir(parents=True)
                    (work/'autostart').mkdir()
                    (work/'auto.sh').touch()
                    if enabled:
                        (work/'state/auto.enabled').touch()
                        (work/'autostart/enabled').touch()
                    (work/'proc/123').mkdir(parents=True)
                    (work/'proc/mounts').write_text('tmpfs /dev tmpfs rw 0 0\n')
                    (work/'proc/123/stat').write_text('123 (app) ' + '0 '*19 + '99\n')
                    (work/'proc/123/maps').write_text('/system/lib64/libsecxrcontrollerservice_jni.xrcontroller.samsung.so\n/system/lib64/libopenxr_android.so\n')
                    script = '''id() { echo 0; }
pidof() { echo 123; }
ps() { echo '123 com.valvesoftware.steamlinkvr'; echo '124 com.valvesoftware.steamlinkvr:remote'; }
getprop() { echo 1; }
sh() { echo disabled > manual; rm -f state/auto.enabled autostart/enabled; }
sha256sum() {
  case "$1" in
    -c) return 0 ;;
    */secxrcontroller.apk) echo 8130b5542c36235eb1e951b9245684299d6a7ddd1a8c95f231e8241e477673cb ;;
    */libsecxrcontrollerservice_jni.xrcontroller.samsung.so) echo 80d45131558b28107cf7e3c9a2c483bce4e2207b934e4f25e951f0fc79d1cd36 ;;
    */libopenxr_android.so) echo a7dabaa1030cc7979efe987f212488c61b61e8afb9aa3add59f7ae4fbeff4c92 ;;
  esac
}
native() { echo "$2" > called; }
'''
                    script += f"set -- '{mode}' '{identity}'\n"
                    script += (ROOT/'modules'/module/'module/action.sh').read_text().replace(
                        f'/dev/{state}', 'state').replace('/dev/gxr_openxr_transaction', 'global-lock').replace('/proc/', 'proc/').replace(
                        f'"$MODDIR/bin/{binary}"', 'native')
                    (work/'test.sh').write_text(script, newline='\n')
                    result = subprocess.run([SHELL, './test.sh'], cwd=work, capture_output=True, text=True, timeout=10)
                    self.assertEqual((work/'called').exists(), allowed, result.stdout + result.stderr)
                    if mode == 'off':
                        self.assertFalse((work/'autostart/enabled').exists())
                        self.assertTrue((work/'manual').exists())


if __name__ == '__main__':
    unittest.main()
