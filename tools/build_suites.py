"""Build disabled suites from current component sources, in an isolated staging tree."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    subprocess.run([str(x) for x in args], check=True)


def compile_native(source, output, ndk, includes=()):
    tc = ndk / 'toolchains/llvm/prebuilt/windows-x86_64'
    clang = tc / 'bin/clang++.exe'
    resource = subprocess.check_output([str(clang), '-print-resource-dir'], text=True).strip()
    if not Path(resource).is_absolute():
        resource = str(tc / resource)
    driver = [clang, '-ccc-install-dir', tc / 'bin', '--target=aarch64-linux-android34',
        f'--sysroot={tc / "sysroot"}', '-resource-dir', resource, '-std=c++17', '-O2',
        '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation']
    obj = output.with_suffix('.o')
    run(*driver, *[f'-I{x}' for x in includes], '-c', source, '-o', obj)
    run(*driver, f'--ld-path={tc / "bin/ld.lld.exe"}', '-static-libstdc++',
        '-Wl,-z,max-page-size=16384', obj, '-o', output)
    run(tc / 'bin/llvm-strip.exe', '--strip-unneeded', output)


def build(args):
    source = ROOT / 'modules' / args.suite
    spec = json.loads((source / 'suite.json').read_text())
    out = ROOT / 'dist' / args.suite
    out.mkdir(parents=True, exist_ok=True)
    staging = out / 'source'
    # Only overwrite our generated files; never remove historical evidence or other work.
    for name in set(spec['components']) - {'tail-velocity'} | {'openxr-velocity-frame'}:
        shutil.copytree(ROOT / 'modules' / name, staging / 'modules' / name,
                        dirs_exist_ok=True, ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
    shutil.copytree(ROOT / 'tools', staging / 'tools', dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
    p = staging / 'tools/build.py'
    text = p.read_text().replace("subprocess.check_output([str(toolchain/'bin'/('clang++'+ext)), '-print-resource-dir'], text=True).strip()",
                                 "str(toolchain/'lib/clang'/('21' if config['ndk'].startswith('29.') else '19'))")
    p.write_text(text, newline='\n')
    virtual = f'/data/adb/modules/{spec["id"]}/state/components'
    identities = {}
    for component in spec['components']:
        if component == 'tail-velocity':
            continue
        d = staging / 'modules' / component
        props = dict(line.split('=', 1) for line in (d / 'module/module.prop').read_text().splitlines() if '=' in line)
        old = props['id']
        identities[component] = old
        for p in d.rglob('*'):
            if p.is_file() and p.suffix in ('.cpp', '.h', '.inc', '.sh'):
                text = p.read_text()
                text = text.replace(f'/data/adb/modules/{old}', f'{virtual}/{component}')
                text = text.replace(f'/dev/{old}', f'/dev/{spec["id"]}_{component}')
                p.write_text(text, newline='\n')
    if args.suite == 'controller-suite':
        d = staging / 'modules/hal-cache-bypass/native'
        p = d / 'control.inc'
        text = p.read_text()
        a, b = text.index('static int validate('), text.index('static bool change(')
        text = text[:a] + (source / 'native/cache-tail-validation.inc').read_text() + text[b:]
        text = text.replace('if(argc==2&&!strcmp(argv[1],"--selftest"))return selftest();',
                            'if(argc==2&&!strcmp(argv[1],"--compat-selftest"))return compatSelftest();\n'
                            '  if(argc==2&&!strcmp(argv[1],"--selftest"))return selftest();')
        p.write_text(text, newline='\n')
        for name in ('payload.h', 'rx_policy.h'):
            shutil.copyfile(source / 'native/tail' / name, d / name)
    packages = {}
    for component in spec['components']:
        if component == 'tail-velocity':
            binary = out / 'gxr-tail-velocity'
            compile_native(source / 'native/tail/helper.cpp', binary, args.ndk28)
            files = {p.name: p.read_bytes() for p in (source / 'tail-module').iterdir() if p.is_file()}
            files['bin/gxr-tail-velocity'] = binary.read_bytes()
            files['cold-boot-reset.sh'] = (ROOT / 'tools/cold-boot-reset.sh').read_bytes()
            files['gate.sh'] = (ROOT / 'modules/hal-cache-bypass/module/gate.sh').read_bytes()
            packages[component] = files
            continue
        d = staging / 'modules' / component
        if component == 'openxr-velocity-frame':
            run(sys.executable, d / 'build_module.py', '--ndk', args.ndk28, '--runtime', args.runtime)
        elif component == 'steamlink-pose-time':
            # Current source default stays 15 ms; suite writes its explicit 0 ms setting
            # before starting the component, preserving the original payload semantics.
            run(sys.executable, d / 'build.py', '--ndk', args.ndk28, '--runtime', args.runtime, '--scene', args.scene)
        else:
            config = json.loads((d / 'build.json').read_text())
            ndk = args.ndk29 if config['ndk'].startswith('29.') else args.ndk28
            if component == 'controller-extrapolation':
                run(sys.executable, staging / 'modules/openxr-velocity-frame/prepare.py', '--ndk', args.ndk28, '--runtime', args.runtime)
            run(sys.executable, staging / 'tools/build.py', component, '--ndk', ndk)
        archives = list((staging / 'dist' / component).glob('*-auto.zip'))
        archive = max(archives, key=lambda p: p.stat().st_mtime_ns)
        with zipfile.ZipFile(archive) as z:
            packages[component] = {n: z.read(n) for n in z.namelist() if n not in ('disable', 'SHA256SUMS') and not n.startswith('webroot/')}

    payload = {p.relative_to(source / 'module').as_posix(): p.read_bytes()
               for p in (source / 'module').rglob('*') if p.is_file()}
    for component, files in packages.items():
        files.pop('disable', None)
        files['SHA256SUMS'] = ''.join(f'{hashlib.sha256(v).hexdigest()}  {n}\n' for n, v in sorted(files.items()) if n != 'SHA256SUMS').encode()
        payload[f'{component}.files'] = ''.join(n + '\n' for n in sorted(files)).encode()
        for name, data in files.items():
            payload['bin/' + component + '--' + name.replace('/', '-')] = data
    payload['suite.conf'] = (
        f'SUITE_ID={spec["id"]}\nSTART_ORDER="{" ".join(spec["components"])}"\n'
        f'STOP_ORDER="{" ".join(reversed(spec["components"]))}"\n'
        f'REPLACES="{" ".join(spec["replaces"])}"\n'
        f'SETTING_KEYS="{" ".join(spec["defaults"])}"\n').encode()
    for source_name, name in [('suite-auto.sh', 'suite-runtime.sh'), ('suite-settings.sh', 'settings.sh'), ('cold-boot-reset.sh', 'cold-boot-reset.sh')]:
        payload[name] = (ROOT / 'tools' / source_name).read_bytes()
    payload.update(disable=b'', skip_mount=b'')
    payload['SHA256SUMS'] = ''.join(f'{hashlib.sha256(v).hexdigest()}  {n}\n' for n, v in sorted(payload.items()) if n != 'disable').encode()
    props = dict(line.split('=', 1) for line in payload['module.prop'].decode().splitlines() if '=' in line)
    archive = out / f'{spec["id"].replace("_", "-")}-{props["version"]}-auto.zip'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
        for name, data in sorted(payload.items()):
            info = zipfile.ZipInfo(name, (2026, 10, 18, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = (0o100755 if name.endswith('.sh') or name.startswith('bin/') else 0o100644) << 16
            z.writestr(info, data)
    archive.with_suffix('.zip.sha256').write_text(hashlib.sha256(archive.read_bytes()).hexdigest() + '  ' + archive.name + '\n')
    (out / 'build-receipt.json').write_text(json.dumps({'archive': str(archive), 'state': 'LOCAL_BUILD_NOT_DEPLOYED', 'components': list(packages), 'files': {n: hashlib.sha256(v).hexdigest() for n, v in payload.items()}}, indent=2) + '\n')
    print(archive)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('suite', choices=['controller-suite', 'steamlink-suite'])
    for name in ('ndk28', 'ndk29', 'runtime', 'scene'):
        p.add_argument('--' + name, type=Path, required=True)
    build(p.parse_args())
