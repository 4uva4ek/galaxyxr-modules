"""Build disabled-by-default variants without changing other modules' builder."""
from pathlib import Path
import hashlib
import subprocess
import sys
import zipfile
import argparse, os

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--ndk',default=os.environ.get('ANDROID_NDK_HOME'),required=not bool(os.environ.get('ANDROID_NDK_HOME')))
args=parser.parse_args()
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
subprocess.run([sys.executable, str(ROOT/'tools/build.py'), 'hal-prediction-30',
                '--ndk', args.ndk,
                '--variant', 'auto'], check=True)
for variant in ('auto',):
    path = ROOT/'dist/hal-prediction-30'/f'gxr-hal-prediction-30-0.1.1-{variant}.zip'
    with zipfile.ZipFile(path) as z:
        payload = {n: z.read(n) for n in z.namelist()}
    props = dict(line.split('=', 1) for line in payload['module.prop'].decode().splitlines())
    props['description'] = ('Installed disabled. Manual checkbox activation; no startup. Shared controller HAL request +30 ms.'
                            if variant == 'manual' else
                            'Installed disabled. Checkbox applies/restores shared controller HAL +30 ms. After-boot automatic worker; disable before reboot for rollback.')
    payload['module.prop'] = ''.join(f'{k}={v}\n' for k,v in props.items()).encode()
    # The KernelSU marker must be mutable; it is an activation preference, not code.
    payload['SHA256SUMS'] = ''.join(f'{hashlib.sha256(data).hexdigest()}  {name}\n'
                                   for name,data in sorted(payload.items())
                                   if name not in ('SHA256SUMS', 'disable')).encode()
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
        for name,data in sorted(payload.items()):
            info = zipfile.ZipInfo(name, date_time=(2026,10,11,0,0,0))
            info.create_system = 3
            info.compress_type = zipfile.ZIP_DEFLATED
            mode = 0o755 if name.endswith('.sh') or name.startswith('bin/') else 0o644
            info.external_attr = (0o100000 | mode) << 16
            z.writestr(info, data)
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    path.with_suffix('.zip.sha256').write_text(f'{digest}  {path.name}\n', encoding='ascii')
    print(f'Final {variant}: {digest}')
