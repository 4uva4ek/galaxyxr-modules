"""Build one standalone KernelSU ZIP. Usage: python tools/build.py haptic-main."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('module', choices=sorted(p.name for p in (ROOT/'modules').iterdir() if (p/'build.json').is_file()))
parser.add_argument('--ndk', default=os.environ.get('ANDROID_NDK_HOME'))
parser.add_argument('--variant', choices=['auto'], default='auto')
args = parser.parse_args()
if not args.ndk:
    parser.error('Set ANDROID_NDK_HOME or pass --ndk PATH')
source = ROOT/'modules'/args.module
config = json.loads((source/'build.json').read_text())
props = dict(line.split('=', 1) for line in (source/'module/module.prop').read_text().splitlines() if '=' in line)
ndk = Path(args.ndk).resolve()
revision = (ndk/'source.properties').read_text()
if f'Pkg.Revision = {config["ndk"]}' not in revision:
    parser.error(f'Expected Android NDK {config["ndk"]}')
host = {'Windows':'windows-x86_64', 'Linux':'linux-x86_64', 'Darwin':'darwin-x86_64'}[platform.system()]
toolchain = ndk/'toolchains/llvm/prebuilt'/host
compiler = toolchain/'bin'/f'aarch64-linux-android{config["api"]}-clang++'
if os.name == 'nt': compiler = compiler.with_suffix('.cmd')
output = ROOT/'dist'/args.module
output.mkdir(parents=True, exist_ok=True)
binary = output/config['binary']
ext = '.exe' if os.name == 'nt' else ''
driver = [str(toolchain/'bin'/('clang++'+ext)), '-ccc-install-dir', str(toolchain/'bin'),
          f'--target=aarch64-linux-android{config["api"]}', f'--sysroot={toolchain/"sysroot"}',
          '-resource-dir', subprocess.check_output([str(toolchain/'bin'/('clang++'+ext)), '-print-resource-dir'], text=True).strip()]
subprocess.run(driver + ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                '-c', str(source/'native/main.cpp'), '-o', str(output/'main.o')], check=True)
subprocess.run(driver + [f'--ld-path={toolchain/"bin"/("ld.lld"+ext)}', '-static-libstdc++',
                '-Wl,-z,max-page-size=16384', '-Wl,--build-id=sha1',
                str(output/'main.o')] + ['-l'+lib for lib in config.get('link_libraries', [])] +
                ['-o', str(binary)], check=True)
strip = toolchain/'bin'/('llvm-strip.exe' if os.name == 'nt' else 'llvm-strip')
subprocess.run([str(strip), '--strip-unneeded', str(binary)], check=True)
blob = binary.read_bytes()
assert blob[:6] == b'\x7fELF\x02\x01' and blob[18:20] == b'\xb7\x00'
if config.get('variants') != ['auto']:
    parser.error(f'{args.module} must declare only disabled automatic packaging')
variants = ['auto']
for variant in variants:
    payload = {name:(source/'module'/name).read_bytes() for name in config['files']}
    for name in config.get('shared_files', []):
        payload[name] = (ROOT/'tools'/name).read_bytes()
    if variant == 'auto':
        payload.update({name:(source/'module'/name).read_bytes() for name in config['auto_files']})
        payload.update({name:(ROOT/'tools'/name).read_bytes() for name in config['auto_shared_files']})
        payload['disable'] = b''
    if variant:
        package_props = dict(props)
        package_props['name'] += f' ({variant})'
        payload['module.prop'] = ''.join(f'{k}={v}\n' for k,v in package_props.items()).encode()
    assert not any(b'\r' in data for data in payload.values()), 'Source must use LF line endings'
    # Optional local payloads are pinned separately and excluded from text checks.
    for name, expected in config.get('binary_files', {}).items():
        data = (source/'module'/name).read_bytes()
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError(f'Pinned binary payload mismatch: {name}')
        payload[name] = data
    payload['bin/'+config['binary']] = blob
    payload['NOTICE.txt'] = (source/'NATIVE-NOTICE.txt').read_bytes()
    payload['NDK-NOTICE.txt'] = (toolchain/'NOTICE').read_bytes()
    payload['SHA256SUMS'] = ''.join(f'{hashlib.sha256(b).hexdigest()}  {n}\n' for n,b in sorted(payload.items()) if n != 'disable').encode()
    suffix = f'-{variant}' if variant else ''
    archive = output/f'{config["artifact"]}-{props["version"]}{suffix}.zip'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
        for name, data in sorted(payload.items()):
            info = zipfile.ZipInfo(name, date_time=(2026,10,10,0,0,0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            mode = 0o755 if name.endswith('.sh') or name == 'bin/'+config['binary'] else 0o644
            info.external_attr = (0o100000 | mode) << 16
            z.writestr(info, data)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix('.zip.sha256').write_text(f'{digest}  {archive.name}\n', encoding='ascii')
    print(archive)
    print('SHA256:', digest)
