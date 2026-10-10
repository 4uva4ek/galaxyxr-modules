import hashlib
import pathlib
import zipfile

root = pathlib.Path(__file__).resolve().parents[3]
module = pathlib.Path(__file__).resolve().parents[1]
props = dict(line.split('=', 1) for line in (module/'module/module.prop').read_text().splitlines() if '=' in line)
with zipfile.ZipFile(root / f'dist/haptic-main/gxr-haptic-main-{props["version"]}.zip') as z:
    assert z.testzip() is None
    names = set(z.namelist())
    assert names == {'action.sh','customize.sh','module.prop','skip_mount','bin/gxr-main','SHA256SUMS','NOTICE.txt','NDK-NOTICE.txt'}
    for forbidden in ['service.sh', 'post-fs-data.sh', 'boot-completed.sh', 'post-mount.sh', 'system.prop', 'sepolicy.rule']:
        assert forbidden not in names
    assert not any(n.startswith(('system/', 'META-INF/')) or '..' in n for n in names)
    assert z.read('skip_mount') == b''
    covered = set()
    for line in z.read('SHA256SUMS').decode().splitlines():
        digest, name = line.split('  ', 1)
        assert hashlib.sha256(z.read(name)).hexdigest() == digest, name
        covered.add(name)
    assert covered == names - {'SHA256SUMS'}
    b = z.read('bin/gxr-main')
    assert b[:6] == b'\x7fELF\x02\x01' and b[18:20] == b'\xb7\x00'
    assert not any('frida' in n.lower() for n in names)
    for name in ['customize.sh', 'action.sh']:
        assert b'\r' not in z.read(name)
    assert b'8130b5542c36235eb1e951b9245684299d6a7ddd1a8c95f231e8241e477673cb' in z.read('action.sh')
print('ZIP integrity, complete checksums, ARM64 binary, no boot hooks, mounts or Frida: OK')
