"""Build the automatic-only, installed-disabled OTA control package.

Shell-only module: no NDK, no native payload. Usage from the repository root:

    python modules/ota-control/build.py

Produces dist/ota-control/gxr-ota-control-VERSION-auto.zip and its .zip.sha256.
"""
import hashlib
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
PROPS = dict(line.split('=', 1) for line in (HERE / 'module/module.prop').read_text('utf-8').splitlines() if '=' in line)
payload = {}
for path in sorted((HERE / 'module').rglob('*')):
    if not path.is_file():
        continue
    data = path.read_bytes()
    if b'\r' in data:
        raise ValueError(f'{path.name}: source must use LF line endings')
    payload[path.relative_to(HERE / 'module').as_posix()] = data
for required in ('module.prop', 'service.sh', 'action.sh', 'ota-ctl.sh', 'uninstall.sh', 'customize.sh', 'auto.sh', 'skip_mount'):
    if required not in payload:
        raise ValueError(f'missing {required}')
if payload['skip_mount']:
    raise ValueError('skip_mount must be an empty marker')
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
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
archive.with_suffix('.zip.sha256').write_text(f'{digest}  {archive.name}\n', encoding='ascii')
print(archive)
print('SHA256:', digest)
