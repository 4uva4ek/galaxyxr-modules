"""Build the installed-disabled MV-HEVC eye fix package.

Overlay-only module: no NDK, no native build. Usage from the repository root:

    python modules/mvhevc-eye-fix/build.py

Produces dist/mvhevc-eye-fix/gxr-mvhevc-eye-fix-VERSION-auto.zip and its .zip.sha256.
Root files only: the patched libraries are generated on the device from the stock
libraries, so no vendor binaries are ever packaged; a bin/ directory is rejected.
"""
import hashlib
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
PROPS = {k: v for k, v in (line.split('=', 1) for line in (HERE / 'module/module.prop').read_text('utf-8').splitlines() if '=' in line)}
TEXT_SUFFIXES = {'.sh', '.prop', '.txt', '.md'}
payload = {}
for path in sorted((HERE / 'module').rglob('*')):
    if not path.is_file():
        continue
    data = path.read_bytes()
    if path.suffix.lower() in TEXT_SUFFIXES and b'\r' in data:
        raise ValueError(f'{path}: source must use LF line endings')
    payload[path.relative_to(HERE / 'module').as_posix()] = data
for required in ('module.prop', 'customize.sh', 'service.sh', 'action.sh', 'auto.sh', 'uninstall.sh', 'skip_mount', 'install-info.txt'):
    if required not in payload:
        raise ValueError(f'missing {required}')
packaged_binaries = sorted(n for n in payload if n.startswith('bin/'))
if packaged_binaries:
    raise ValueError('binary payloads must not be packaged (' + ', '.join(packaged_binaries)
                     + '); the device generates them from the stock libraries; see module/NOTICE.txt')
if payload['skip_mount'] != b'':
    raise ValueError('skip_mount marker must be empty')
bad_dirs = [n for n in payload if '/' in n and not n.startswith('bin/')]
if bad_dirs:
    raise ValueError(f'unsupported directory entries (GXR contract allows bin/ only): {bad_dirs}')
package_props = dict(PROPS)
package_props['name'] += ' (auto)'
payload['module.prop'] = ''.join(f'{k}={v}\n' for k, v in package_props.items()).encode()
payload['disable'] = b''
payload['SHA256SUMS'] = ''.join(f'{hashlib.sha256(b).hexdigest()}  {n}\n' for n, b in sorted(payload.items()) if n != 'disable').encode()
output = ROOT / 'dist' / HERE.name
output.mkdir(parents=True, exist_ok=True)
archive = output / f'gxr-{HERE.name}-{PROPS["version"]}-auto.zip'
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
    for name, data in sorted(payload.items()):
        info = zipfile.ZipInfo(name, date_time=(2026, 10, 10, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        info.create_system = 3
        mode = 0o755 if name.endswith('.sh') else 0o644
        info.external_attr = (0o100000 | mode) << 16
        z.writestr(info, data)
# verify the built package in code: hashes, markers, CR scan on text members
with zipfile.ZipFile(archive) as z:
    names = z.namelist()
    sums = {n: h for h, n in (line.split('  ', 1) for line in z.read('SHA256SUMS').decode().splitlines() if line)}
    for name in names:
        if name in ('disable', 'SHA256SUMS'):
            continue
        digest = hashlib.sha256(z.read(name)).hexdigest()
        if sums.get(name) != digest:
            raise ValueError(f'SHA256SUMS mismatch for {name}: listed={sums.get(name)} actual={digest}')
    if z.read('disable') != b'' or z.read('skip_mount') != b'':
        raise ValueError('disable/skip_mount markers must be empty')
    for name in names:
        if Path(name).suffix.lower() in TEXT_SUFFIXES and b'\r' in z.read(name):
            raise ValueError(f'CR byte in {name}')
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
archive.with_suffix('.zip.sha256').write_text(f'{digest}  {archive.name}\n', encoding='ascii')
print(archive)
print('SHA256:', digest)
