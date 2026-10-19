"""Host-only checks of the Haptics MAIN worker: idle while the patch is live,
re-applied after a sudden Android restart, still contained after a service crash."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

MODULE = Path(__file__).resolve().parents[1]
SHELL = os.environ.get('GXR_TEST_SHELL') or shutil.which('sh') or 'C:/Program Files/Git/bin/bash.exe'
HEAVY = ('getprop', 'pidof', 'sync', 'sha256sum')
# Controller service 123 -> 456, system_server 1 -> 2. Uptime advances 5 s per sleep.
SERVICE_CRASH = '[ "$tick" -ne 20 ] || { rm -f proc/123/stat; echo 456 > pid; }'
SUDDEN_RESTART = '[ "$tick" -ne 20 ] || { rm -f proc/123/stat proc/1/stat; echo 456 > pid; echo 2 > android; }'


def heavy(line):
    return line.split(' ', 1)[0] in HEAVY


class LightWorker(unittest.TestCase):
    def run_case(self, steps=':', settings=''):
        with tempfile.TemporaryDirectory() as temp:
            work = Path(temp)
            for directory in ('state/auto.worker', 'autostart', 'proc/123', 'proc/456', 'proc/1', 'proc/2'):
                (work/directory).mkdir(parents=True)
            for name in ('state/auto.enabled', 'autostart/enabled'):
                (work/name).touch()
            files = {
                'proc/mounts': 'tmpfs /dev tmpfs rw 0 0',
                'proc/uptime': '100.0 0',
                'proc/123/stat': '123 (controller svc) ' + '0 '*19 + '99',
                'proc/456/stat': '456 (controller svc) ' + '0 '*19 + '111',
                'proc/1/stat': '1 (system_server) ' + '0 '*19 + '5',
                'proc/2/stat': '2 (system_server) ' + '0 '*19 + '300',
                'proc/123/maps': 'JNI',
                'proc/456/maps': 'JNI',
                'pid': '123',
                'android': '1',
            }
            for name, text in files.items():
                (work/name).write_text(text + '\n', newline='\n')
            code = '''MODDIR=.
STATE=state
PACKAGE=test.service
LIBRARY=JNI
SYSTEM_SERVICE=1
SOFT_RESTART_AWARE=1
TARGET_READY_TIMEOUT=20
SETTINGS
id() { echo 0; }
getprop() { echo getprop >> trace; echo 1; }
pidof() { echo "pidof $1" >> trace; if [ "$1" = system_server ]; then cat android; else cat pid; fi; }
sync() { echo sync >> trace; }
sha256sum() { echo sha256sum >> trace; return 0; }
sh() {
  case "$2" in
    _auto-off) echo off >> trace; return 0 ;;
    _auto-on) echo "on $3" >> trace; return 0 ;;
    _auto-check) echo "check $tick" >> trace; echo "MAIN pid=${3%%:*} site=1 profile=step2-amplitude-stock"; return 0 ;;
    *) return 90 ;;
  esac
}
tick=0
sleep() {
  tick=$((tick + 1))
  echo "sleep $1" >> trace
  echo "$((100 + tick * 5)).0 0" > proc/uptime
  STEPS
  [ "$tick" -lt 80 ] || rm -f state/auto.enabled
}
set -- _run
'''.replace('SETTINGS', settings).replace('STEPS', steps)
            code += (MODULE/'module/autostart.sh').read_text().replace('/proc/', 'proc/')
            (work/'run.sh').write_text(code, newline='\n')
            result = subprocess.run([SHELL, './run.sh'], cwd=work, capture_output=True, text=True, timeout=30)
            trace = (work/'trace').read_text().splitlines()
            return result, trace, (work/'disable').exists()

    def steady(self, trace, activation='on 123:99'):
        """Trace from the first poll after an activation's own bookkeeping."""
        rest = trace[trace.index(activation) + 1:]
        return rest[next(i for i, line in enumerate(rest) if line.startswith('sleep')) + 1:]

    def test_steady_state_runs_no_external_checks(self):
        result, trace, disabled = self.run_case()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse(disabled)
        steady = self.steady(trace)
        self.assertEqual([line for line in steady if heavy(line)], [], steady)
        self.assertEqual({line for line in steady if line.startswith('sleep')}, {'sleep 5'})
        # First check 30 s after activation, then every 300 s.
        self.assertEqual([line for line in steady if line.startswith('check')], ['check 6', 'check 66'])
        self.assertEqual(steady[-1], 'off')

    def test_unchecked_module_restores_within_one_poll(self):
        result, trace, disabled = self.run_case('[ "$tick" -ne 10 ] || touch disable')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(disabled)
        self.assertEqual(trace[-1], 'off')
        self.assertEqual(len([line for line in trace if line.startswith('sleep')]), 10)

    def test_service_crash_alone_still_disables_after_grace(self):
        result, trace, disabled = self.run_case(SERVICE_CRASH)
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(disabled)
        self.assertIn('Target exited/restarted after patch', result.stdout)
        self.assertNotIn('on 456:111', trace)
        self.assertEqual(trace[-1], 'off')

    def test_sudden_android_restart_applies_patch_again(self):
        result, trace, disabled = self.run_case(SUDDEN_RESTART)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse(disabled)
        self.assertIn('Android restarted without a KernelSU notice', result.stdout)
        self.assertEqual([line for line in trace if line.startswith('on ')], ['on 123:99', 'on 456:111'])
        # Back to the idle loop on the new process, health checks included.
        steady = self.steady(trace, 'on 456:111')
        self.assertEqual([line for line in steady if heavy(line)], [], steady)
        self.assertEqual([line for line in steady if line.startswith('check')], ['check 26'])

    def test_sudden_restart_while_system_server_is_down(self):
        result, trace, disabled = self.run_case('''[ "$tick" -ne 20 ] || { rm -f proc/123/stat proc/1/stat; : > pid; : > android; }
[ "$tick" -ne 24 ] || { echo 2 > android; }
[ "$tick" -ne 27 ] || { echo 456 > pid; }''')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse(disabled)
        self.assertIn('Android is restarting', result.stdout)
        self.assertEqual([line for line in trace if line.startswith('on ')], ['on 123:99', 'on 456:111'])

    def test_restart_soon_after_patch_is_contained(self):
        result, trace, disabled = self.run_case(SUDDEN_RESTART.replace('-ne 20', '-ne 5'))
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(disabled)
        self.assertIn('Android restarted within 60 seconds of the patch', result.stdout)
        self.assertNotIn('on 456:111', trace)

    def test_repeated_sudden_restarts_are_limited(self):
        result, trace, disabled = self.run_case(SUDDEN_RESTART, 'SUDDEN_LIMIT=0')
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(disabled)
        self.assertIn('more than 0 times', result.stdout)
        self.assertNotIn('on 456:111', trace)


class AutoCheck(unittest.TestCase):
    def run_check(self, identity):
        with tempfile.TemporaryDirectory() as temp:
            work = Path(temp)
            (work/'proc/123').mkdir(parents=True)
            (work/'state').mkdir()
            (work/'auto.sh').touch()
            (work/'proc/mounts').write_text('tmpfs /dev tmpfs rw 0 0\n', newline='\n')
            (work/'proc/123/stat').write_text('123 (controller svc) ' + '0 '*19 + '99\n', newline='\n')
            code = '''id() { echo 0; }
pidof() { echo pidof >> trace; echo 123; }
sha256sum() { echo sha256sum >> trace; return 0; }
native() { echo "native $*" >> trace; echo "MAIN pid=$1 site=1 profile=step2-amplitude-stock"; }
'''
            code += f"set -- _auto-check '{identity}'\n"
            code += (MODULE/'module/action.sh').read_text().replace('/dev/gxr_haptic_main', 'state').replace(
                '/proc/', 'proc/').replace('"$MODDIR/bin/gxr-main"', 'native')
            (work/'test.sh').write_text(code, newline='\n')
            result = subprocess.run([SHELL, './test.sh'], cwd=work, capture_output=True, text=True, timeout=10)
            trace = (work/'trace').read_text().splitlines() if (work/'trace').exists() else []
            return result, trace, (work/'disable').exists()

    def test_reads_status_without_hashing_or_process_scan(self):
        result, trace, disabled = self.run_check('123:99')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(trace, ['native 123 status'])
        self.assertIn('MAIN pid=123', result.stdout)
        self.assertFalse(disabled)

    def test_changed_process_waits_without_reading(self):
        result, trace, disabled = self.run_check('123:98')
        self.assertEqual(result.returncode, 3, result.stdout + result.stderr)
        self.assertEqual(trace, [])
        self.assertFalse(disabled)


if __name__ == '__main__':
    unittest.main()
