"""Local lifecycle/ZIP checks with mocked Android/native boundaries; no headset access."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess,tempfile,zipfile
H=Path(__file__).resolve().parent
R=H.parents[2]
SHELL=os.environ.get('GXR_TEST_SHELL') or shutil.which('sh') or 'C:/Program Files/Git/bin/bash.exe'
S=R/'modules/framebuffer10/module'
PROPS=dict(line.split('=',1) for line in (S/'module.prop').read_text().splitlines() if '=' in line)
Z=R/f'dist/framebuffer10/gxr-framebuffer10-{PROPS["version"]}-auto.zip'
passed=[]
for p in S.glob('*.sh'):
    subprocess.run([SHELL,'-n',p.as_posix()],check=True)
passed.append('All packaged shell scripts parse')
with tempfile.TemporaryDirectory(prefix='package-tests-',dir=H) as temp:
    w=Path(temp)
    # Reset itself and all real startup entries are covered by test_cold_boot_reset.py.
    (w/'cold-boot-reset.sh').write_text('exit 0\n')
    for p in S.iterdir():
        if p.is_file():shutil.copyfile(p,w/p.name)
    (w/'auto.sh').write_text('id() { echo 0; }\n'+(w/'auto.sh').read_text())
    (w/'bin').mkdir()
    (w/'bin/gxr-fb10-gate').write_text('#!/bin/sh\nprintf "%s\\n" "$*" >> "${0%/*}/../calls"\necho "DISABLED - mock baseline"\n')
    (w/'bin/gxr-fb10-gate').chmod(0o755)
    def run(script,*args):
        return subprocess.run([SHELL,(w/script).as_posix(),*args],capture_output=True,text=True,timeout=8)
    (w/'disable').touch()
    assert run('service.sh').returncode==0
    assert not (w/'calls').exists()
    passed.append('Disabled service performs no native call')
    before={p.name:p.read_bytes() for p in w.iterdir() if p.is_file()}
    r=run('action.sh');assert r.returncode==0,r.stdout+r.stderr
    assert before=={p.name:p.read_bytes() for p in w.iterdir() if p.is_file()}
    assert not (w/'calls').exists()
    passed.append('Action is read-only and never starts a session')
    (w/'disable').unlink();(w/'session-lease').mkdir()
    r=run('auto.sh','resume');assert r.returncode==1,r.stdout+r.stderr
    assert (w/'disable').exists() and (w/'failure').exists()
    assert (w/'session-lease').exists()
    assert (w/'calls').read_text().strip()=='--status'
    passed.append('Interrupted lease disables module, preserves evidence and blocks activation')
with tempfile.TemporaryDirectory(prefix='restart-tests-',dir=H) as temp:
    w=Path(temp);(w/'cold-boot-reset.sh').write_text('exit 0\n');(w/'bin').mkdir();(w/'state').mkdir();(w/'payload').mkdir()
    for p in S.glob('*.sh'):shutil.copyfile(p,w/p.name)
    mocks='''id() { echo 0; }
cat() { if [ "$1" = /proc/sys/kernel/random/boot_id ]; then echo 22222222-2222-2222-2222-222222222222; else command cat "$@"; fi; }
getprop() { case "$1" in sys.boot_completed) echo 1;; *) echo stopped;; esac; }
sha256sum() { return 0; }
chown() { return 0; }
chmod() { return 0; }
chcon() { return 0; }
sync() { return 0; }
'''
    script=(w/'auto.sh').read_text().replace('STATE=/dev/gxr-framebuffer10','STATE="'+(('/'+w.drive[0].lower()+w.as_posix()[2:]+'/state') if os.name=='nt' else str(w/'state'))+'"')
    (w/'auto.sh').write_text(mocks+script)
    (w/'bin/gxr-fb10-gate').write_text('''#!/bin/sh
printf '%s\\n' "$*" >> "${0%/*}/../calls"
case "$1" in
 --preflight) echo 'PREFLIGHT OK';;
 --run-session) exit "${FAKE_SESSION_RESULT:-4}";;
 --status) echo 'DISABLED - KernelSU restart completed';;
 --prepare-restart) echo requested;;
 *) exit 64;;
esac
''')
    (w/'bin/gxr-fb10-gate').chmod(0o755)
    for i in range(3):
        r=subprocess.run([SHELL,(w/'auto.sh').as_posix(),'resume'],capture_output=True,text=True,timeout=5)
        assert r.returncode==0,r.stdout+r.stderr
        assert not (w/'disable').exists() and not (w/'session-lease').exists()
    assert (w/'calls').read_text().count('--run-session')==3
    passed.append('Three clean restart handoffs preserve checkbox and release lease for the next service stage')
    before=(w/'calls').read_text()
    r=subprocess.run([SHELL,(w/'emulated-soft-reboot.sh').as_posix()],capture_output=True,text=True,timeout=5)
    assert r.returncode==0 and (w/'calls').read_text()==before+'--prepare-restart\n'
    passed.append('Standard pre-reboot hook only notifies native guardian; never launches a new session')
    env=os.environ.copy();env['FAKE_SESSION_RESULT']='2'
    r=subprocess.run([SHELL,(w/'auto.sh').as_posix(),'resume'],capture_output=True,text=True,timeout=5,env=env)
    assert r.returncode==1 and (w/'disable').exists() and (w/'session-lease').exists()
    before=(w/'calls').read_text()
    subprocess.run([SHELL,(w/'service.sh').as_posix()],capture_output=True,check=True,timeout=5)
    assert (w/'calls').read_text()==before
    passed.append('Failed handoff disables preference, keeps lease and subsequent service does not retry')
with zipfile.ZipFile(Z) as z:
    names=set(z.namelist())
    assert {'disable','skip_mount','service.sh','emulated-soft-reboot.sh','action.sh','payload/libvulkan_compositor.so'}<=names
    assert not names.intersection({'system.prop','post-fs-data.sh','sepolicy.rule','gxr-live-control.sh','gxr-root-ready.sh'})
    manifest={line.split('  ',1)[1]:line.split('  ',1)[0] for line in z.read('SHA256SUMS').decode().splitlines()}
    assert set(manifest)==names-{'disable','SHA256SUMS'}
    for name,digest in manifest.items():
        assert hashlib.sha256(z.read(name)).hexdigest()==digest,name
    assert manifest['payload/libvulkan_compositor.so']=='ff8e344f81c4f2ce1c3e355a70b73c3de957e800efd736bb11dd874748d6fe4c'
    assert '--run-session' in z.read('auto.sh').decode()
    assert ('version='+PROPS['version']) in z.read('module.prop').decode()
    assert {'restart-prepare.sh','restart-stopped.sh','restart-started.sh','restart-abort.sh'} <= names
    assert 'auto.sh" resume' not in z.read('service.sh').decode()
    for name in names:
        assert not name.startswith('/') and '..' not in Path(name).parts
passed.append('Complete disabled ZIP manifest and tested color library digest verified')
passed.append('Continuous session entry with standard KernelSU soft-reboot notification; no overlay/property file')
result={'scope':'Local mocks and archive only; no headset IO','passed':passed,'zip_sha256':hashlib.sha256(Z.read_bytes()).hexdigest()}
(H/'module-tests.json').write_text(json.dumps(result,indent=2)+'\n')
for item in passed:print('PASS:',item)
