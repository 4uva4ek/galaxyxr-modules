"""Run production cold reset and each startup entry against fake OS boundaries."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SHELL = os.environ.get('GXR_TEST_SHELL') or shutil.which('sh') or 'C:/Program Files/Git/bin/bash.exe'
OLD = '11111111-1111-1111-1111-111111111111'
NEW = '22222222-2222-2222-2222-222222222222'
GUARDS = ('autostart/pending', 'autostart/failed', 'auto.pending',
          'state/active.lease', 'state/failure', 'session-lease/boot-id', 'failure')


class ColdBootReset(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        for name in ('proc/sys/kernel/random', 'ram', 'autostart', 'state'):
            (self.work/name).mkdir(parents=True)
        (self.work/'proc/mounts').write_text('tmpfs /dev tmpfs rw 0 0\n')
        (self.work/'proc/sys/kernel/random/boot_id').write_text(NEW+'\n')
        self.helper = 'id() { echo 0; }\nsync() { :; }\n'+(ROOT/'tools/cold-boot-reset.sh').read_text().replace('/proc/', 'proc/')
        (self.work/'cold-boot-reset.sh').write_text(self.helper, newline='\n')

    def seed(self, boot=OLD):
        if boot is not None:
            (self.work/'guard-boot-id').write_text(boot+'\n')
        for name in GUARDS:
            path=self.work/name
            path.parent.mkdir(exist_ok=True)
            path.write_text('evidence\n')

    def run_reset(self):
        return subprocess.run([SHELL, './cold-boot-reset.sh', '.', 'ram'], cwd=self.work,
                              capture_output=True, text=True, timeout=10)

    def test_new_boot_clears_all_guards_and_preserves_each_checkbox_state(self):
        for disabled in (False, True):
            self.seed()
            if disabled: (self.work/'disable').write_text('manual preference\n')
            (self.work/'remove').write_text('preserve removal\n')
            r=self.run_reset()
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
            self.assertFalse(any((self.work/n).exists() for n in GUARDS))
            self.assertFalse((self.work/'session-lease').exists())
            self.assertEqual((self.work/'disable').exists(),disabled)
            if disabled:self.assertEqual((self.work/'disable').read_text(),'manual preference\n')
            self.assertEqual((self.work/'remove').read_text(),'preserve removal\n')
            self.assertEqual((self.work/'guard-boot-id').read_text().strip(),NEW)

    def test_same_boot_including_soft_reboot_keeps_every_guard(self):
        self.seed(NEW)
        for _ in range(3):
            r=self.run_reset()
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
            self.assertTrue(all((self.work/n).read_text()=='evidence\n' for n in GUARDS))

    def test_second_entry_cannot_clear_new_failure(self):
        self.seed()
        self.assertEqual(self.run_reset().returncode,0)
        (self.work/'autostart/pending').write_text('new attempt')
        self.assertEqual(self.run_reset().returncode,0)
        self.assertEqual((self.work/'autostart/pending').read_text(),'new attempt')

    def test_fresh_package_records_boot_before_first_activation(self):
        self.assertEqual(self.run_reset().returncode,0)
        self.assertEqual((self.work/'guard-boot-id').read_text().strip(),NEW)

    def test_legacy_untagged_evidence_is_not_misclassified_as_cold(self):
        self.seed(None)
        self.assertNotEqual(self.run_reset().returncode,0)
        self.assertTrue(all((self.work/n).exists() for n in GUARDS))
        self.assertFalse((self.work/'guard-boot-id').exists())

    def test_invalid_boot_ids_preserve_evidence(self):
        for saved,current in [('garbage',NEW),(OLD,'')]:
            self.seed(saved)
            (self.work/'proc/sys/kernel/random/boot_id').write_text(current)
            self.assertNotEqual(self.run_reset().returncode,0)
            self.assertTrue(all((self.work/n).exists() for n in GUARDS))

    def test_unexpected_lease_contents_not_recursively_removed(self):
        self.seed()
        (self.work/'session-lease/unexpected').write_text('preserve')
        self.assertNotEqual(self.run_reset().returncode,0)
        self.assertTrue(all((self.work/n).exists() for n in GUARDS))
        self.assertEqual((self.work/'guard-boot-id').read_text().strip(),OLD)

    def test_concurrent_reset_waits_without_changing_evidence(self):
        self.seed()
        (self.work/'ram/cold-reset').mkdir()
        self.assertEqual(self.run_reset().returncode,2)
        self.assertTrue(all((self.work/n).exists() for n in GUARDS))

    def test_non_tmpfs_refuses_reset(self):
        self.seed()
        (self.work/'proc/mounts').write_text('device /dev ext4 rw 0 0\n')
        self.assertNotEqual(self.run_reset().returncode,0)
        self.assertTrue(all((self.work/n).exists() for n in GUARDS))

    def test_haptics_enabled_cold_start_launches_without_self_disabling(self):
        self.seed()
        source='''id() { echo 0; }
sync() { :; }
nohup() { touch launched; }
MODDIR=.
STATE=ram
set -- resume
'''+(ROOT/'tools/autostart.sh').read_text().replace('/proc/','proc/')+'\nwait\n'
        (self.work/'entry.sh').write_text(source,newline='\n')
        r=subprocess.run([SHELL,'./entry.sh'],cwd=self.work,capture_output=True,text=True,timeout=10)
        self.assertEqual(r.returncode,0,r.stdout+r.stderr)
        self.assertTrue((self.work/'launched').exists())
        self.assertFalse((self.work/'disable').exists())
        self.assertFalse((self.work/'autostart/pending').exists())

    def test_all_six_startup_entries_reset_before_old_guard_checks(self):
        for module in ('haptic-main','controller-extrapolation','openxr-velocity-frame',
                       'controller-whitening','controller-timestamp-precision','framebuffer10'):
            with self.subTest(module=module):
                self.seed()
                # disabled keeps each entry from activation AFTER the helper;
                # guards must nevertheless expire, without changing preference.
                (self.work/'disable').write_text('manual OFF')
                if module in ('haptic-main','controller-extrapolation'):
                    source='MODDIR=.\nSTATE=ram\n'+(ROOT/'tools/autostart.sh').read_text()
                else:
                    source=(ROOT/f'modules/{module}/module/auto.sh').read_text()
                    import re
                    source=re.sub(r'^MODDIR=.*$', 'MODDIR=.', source, flags=re.M)
                    source=re.sub(r'^STATE=.*$', 'STATE=ram', source, flags=re.M)
                source='id() { echo 0; }\nsync() { :; }\n'+source.replace('/proc/','proc/')
                (self.work/'entry.sh').write_text(source,newline='\n')
                r=subprocess.run([SHELL,'./entry.sh','resume'],cwd=self.work,capture_output=True,text=True,timeout=10)
                self.assertEqual(r.returncode,0,r.stdout+r.stderr)
                self.assertFalse(any((self.work/n).exists() for n in GUARDS))
                self.assertEqual((self.work/'disable').read_text(),'manual OFF')

    def test_framebuffer_real_reset_does_not_create_native_session(self):
        # Run the production startup + reset together, fake only OS/native IO.
        # The native boundary enforces main.cpp's existing-directory refusal.
        import re
        (self.work/'bin').mkdir()
        (self.work/'bin/gxr-fb10-gate').write_text('''#!/bin/sh
case "$1" in
 --preflight) exit 0;;
 --run-session)
  if [ -e session ]; then echo 'previous session incomplete or failed'; exit 1; fi
  touch native-start-reached
  exit 4;;
 --status) echo 'DISABLED - mock baseline';;
 *) exit 64;;
esac
''', newline='\n')
        (self.work/'bin/gxr-fb10-gate').chmod(0o755)
        source=(ROOT/'modules/framebuffer10/module/auto.sh').read_text()
        source=re.sub(r'^MODDIR=.*$', 'MODDIR=.', source, flags=re.M)
        source=re.sub(r'^STATE=.*$', 'STATE=session', source, flags=re.M)
        mocks='''id() { echo 0; }
getprop() { case "$1" in sys.boot_completed) echo 1;; *) echo stopped;; esac; }
sha256sum() { return 0; }
chown() { return 0; }
chmod() { return 0; }
chcon() { return 0; }
sync() { return 0; }
'''
        (self.work/'entry.sh').write_text(mocks+source.replace('/proc/','proc/'), newline='\n')
        def run():
            return subprocess.run([SHELL,'./entry.sh','resume'],cwd=self.work,
                                  capture_output=True,text=True,timeout=10)
        for boot in (NEW, NEW, OLD):
            (self.work/'proc/sys/kernel/random/boot_id').write_text(boot+'\n')
            r=run()
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
            self.assertTrue((self.work/'native-start-reached').exists())
            self.assertFalse((self.work/'session').exists())
            self.assertFalse((self.work/'disable').exists())
            self.assertFalse((self.work/'session-lease').exists())
            (self.work/'native-start-reached').unlink()
        # A real incomplete native session is still rejected and preserved.
        (self.work/'session').mkdir()
        (self.work/'session/evidence').write_text('preserve')
        r=run()
        self.assertEqual(r.returncode,1,r.stdout+r.stderr)
        self.assertFalse((self.work/'native-start-reached').exists())
        self.assertTrue((self.work/'disable').exists())
        self.assertTrue((self.work/'session-lease').exists())
        self.assertEqual((self.work/'session/evidence').read_text(),'preserve')


if __name__=='__main__':unittest.main(verbosity=2)
