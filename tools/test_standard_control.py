"""Host-only checks of standard startup and explicit recovery routing."""
import json
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
MODULES = ('haptic-main', 'controller-extrapolation', 'openxr-velocity-frame')


class StandardControl(unittest.TestCase):
    def test_explicit_on_uses_worker_without_manager_hook(self):
        for name in MODULES:
            with self.subTest(module=name), tempfile.TemporaryDirectory() as temp:
                work = Path(temp)
                (work/'proc').mkdir()
                (work/'proc/mounts').write_text('tmpfs /dev tmpfs rw 0 0\n', newline='\n')
                (work/'auto.sh').write_text('printf "%s\\n" "$*" > called\n', newline='\n')
                script = (ROOT/'modules'/name/'module/action.sh').read_text()
                script = script.replace(f'/dev/gxr_{name.replace("-", "_")}', 'state')
                script = script.replace('/proc/mounts', 'proc/mounts')
                (work/'action.sh').write_text('id() { echo 0; }\n' + script, newline='\n')
                result = subprocess.run([SHELL, './action.sh', 'on'], cwd=work,
                                        capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual((work/'called').read_text().strip(), 'enable')
                self.assertFalse((work/'live-control.sh').exists())

    def test_default_action_only_reports_and_preserves_recovery_state(self):
        for name in MODULES:
            with self.subTest(module=name), tempfile.TemporaryDirectory() as temp:
                work = Path(temp)
                for marker in ('disable', 'auto.pending', 'failure', 'active.lease'):
                    (work/marker).write_text('preserve\n', newline='\n')
                (work/'auto.sh').write_text('exit 97\n', newline='\n')
                shutil.copyfile(ROOT/'modules'/name/'module/module.prop', work/'module.prop')
                shutil.copyfile(ROOT/'tools/action-report.sh', work/'action-report.sh')
                # Only the diagnostic status child is allowed. Any on/off dispatch fails.
                prefix = 'sh() { [ "$2" = status ] || return 98; echo "STOCK pid=123"; }\n'
                (work/'action.sh').write_text(prefix + (ROOT/'modules'/name/'module/action.sh').read_text(), newline='\n')
                result = subprocess.run([SHELL, './action.sh'], cwd=work,
                                        capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn('DISABLED pid=123', result.stdout)
                self.assertIn('next KernelSU service stage', result.stdout)
                self.assertIn('does not call a module start/stop hook', result.stdout)
                for marker in ('disable', 'auto.pending', 'failure', 'active.lease'):
                    self.assertEqual((work/marker).read_text(), 'preserve\n')

    def test_source_packages_use_only_standard_entry(self):
        for name in MODULES:
            module = ROOT/'modules'/name
            props = (module/'module/module.prop').read_text()
            self.assertNotIn('gxrLiveControl', props)
            self.assertNotIn('Live control', props)
            self.assertIn('auto.sh" resume', (module/'module/service.sh').read_text())
            self.assertFalse((module/'module/live-control.sh').exists())
            self.assertFalse((module/'module/gxr-root-ready.sh').exists())
            if (module/'build.json').exists():
                config = json.loads((module/'build.json').read_text())
                self.assertEqual(config['variants'], ['auto'])
                self.assertEqual(config['shared_files'], ['action-report.sh'])


if __name__ == '__main__':
    unittest.main()
