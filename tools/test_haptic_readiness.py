"""Production worker readiness scheduling with fake Android boundaries."""
from pathlib import Path
import tempfile, subprocess, unittest, shutil, os
ROOT=Path(__file__).resolve().parents[1]
SHELL=os.environ.get('GXR_TEST_SHELL') or shutil.which('sh') or 'C:/Program Files/Git/bin/bash.exe'

class Readiness(unittest.TestCase):
    def run_case(self, scenario):
        with tempfile.TemporaryDirectory() as tmp:
            w=Path(tmp)
            for d in ('state/auto.worker','autostart','proc/123'):(w/d).mkdir(parents=True)
            for f in ('state/auto.enabled','autostart/enabled'):(w/f).touch()
            (w/'proc/mounts').write_text('tmpfs /dev tmpfs rw 0 0\n')
            (w/'proc/uptime').write_text('100.0 0\n')
            (w/'proc/123/stat').write_text('123 (service) '+'0 '*19+'99\n')
            (w/'proc/123/maps').write_text('JNI\n' if scenario=='ready' else '')
            (w/'pid').write_text('123\n' if scenario in ('ready','library') else '')
            (w/'boot').write_text('1\n')
            steps={
                'ready':':',
                'service':'[ "$tick" != 3 ] || { echo 123 > pid; echo JNI > proc/123/maps; }',
                'library':'[ "$tick" != 3 ] || echo JNI > proc/123/maps',
                'restart':'''[ "$tick" != 7 ] || echo 0 > boot
[ "$tick" != 9 ] || echo 1 > boot
[ "$tick" != 15 ] || { echo 123 > pid; echo JNI > proc/123/maps; }''',
                'timeout':':',
                'cancel':'[ "$tick" != 2 ] || { touch disable; rm state/auto.enabled; }',
            }[scenario]
            code='''MODDIR=.
STATE=state
PACKAGE=test.service
LIBRARY=JNI
SYSTEM_SERVICE=1
SOFT_RESTART_AWARE=1
TARGET_READY_TIMEOUT=20
id() { echo 0; }
getprop() { cat boot; }
pidof() { cat pid; }
sync() { :; }
sha256sum() { return 0; }
sh() {
 case "$2" in
 _auto-off) return 0;;
 _auto-on) echo "$tick:$3" >> calls; rm state/auto.enabled; return 0;;
 *) return 90;; esac
}
tick=0
sleep() {
 tick=$((tick+1))
 echo "$((100+tick*2)).0 0" > proc/uptime
 STEPS
 [ "$tick" -lt 30 ] || rm -f state/auto.enabled
}
set -- _run
'''.replace('STEPS',steps)+(ROOT/'modules/haptic-main/module/autostart.sh').read_text().replace('/proc/','proc/')
            (w/'run.sh').write_text(code,newline='\n')
            r=subprocess.run([SHELL,'./run.sh'],cwd=w,capture_output=True,text=True,timeout=10)
            calls=(w/'calls').read_text().splitlines() if (w/'calls').exists() else []
            return r,calls,(w/'disable').exists()

    def test_immediate_when_ready(self):
        r,c,d=self.run_case('ready');self.assertEqual(r.returncode,0,r.stdout+r.stderr);self.assertEqual(c,['0:123:99']);self.assertFalse(d)
    def test_waits_only_until_service_or_library_ready(self):
        for s in ('service','library'):
            r,c,d=self.run_case(s);self.assertEqual(r.returncode,0,r.stdout+r.stderr);self.assertEqual(c,['3:123:99']);self.assertFalse(d)
    def test_boot_reset_restarts_readiness_budget(self):
        r,c,d=self.run_case('restart');self.assertEqual(r.returncode,0,r.stdout+r.stderr);self.assertEqual(c,['15:123:99']);self.assertFalse(d)
    def test_timeout_contains_failure_without_writes(self):
        r,c,d=self.run_case('timeout');self.assertEqual(r.returncode,1);self.assertEqual(c,[]);self.assertTrue(d);self.assertIn('timed out after 20 seconds',r.stdout)
    def test_disable_cancels_wait_without_activation(self):
        r,c,d=self.run_case('cancel');self.assertEqual(r.returncode,0,r.stdout+r.stderr);self.assertEqual(c,[]);self.assertTrue(d)

if __name__=='__main__':unittest.main(verbosity=2)
