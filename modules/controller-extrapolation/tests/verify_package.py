import hashlib
import pathlib
import zipfile

root = pathlib.Path(__file__).resolve().parents[3]
module = pathlib.Path(__file__).resolve().parents[1]
props = dict(line.split('=', 1) for line in (module/'module/module.prop').read_text().splitlines() if '=' in line)
with zipfile.ZipFile(root / f'dist/controller-extrapolation/gxr-controller-extrapolation-{props["version"]}.zip') as z:
    assert z.testzip() is None
    names = set(z.namelist())
    assert names == {'action.sh','customize.sh','module.prop','skip_mount','bin/gxr-extrapolation','SHA256SUMS','NOTICE.txt','NDK-NOTICE.txt'}
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
    b = z.read('bin/gxr-extrapolation')
    assert b[:6] == b'\x7fELF\x02\x01' and b[18:20] == b'\xb7\x00'
    assert not any('frida' in n.lower() for n in names)
    for name in ['customize.sh', 'action.sh']:
        assert b'\r' not in z.read(name)
    assert b'a7dabaa1030cc7979efe987f212488c61b61e8afb9aa3add59f7ae4fbeff4c92' in z.read('action.sh')
print('ZIP integrity, complete checksums, ARM64 binary, no boot hooks, mounts or Frida: OK')
