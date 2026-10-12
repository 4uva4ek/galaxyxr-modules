"""Validate a published automatic package and advance its KernelSU update feed.

Never execute package code. Release ZIPs remain built/tested locally; this tool
only promotes a published, reviewed artifact into the stable update catalogue.
"""
import argparse
import hashlib
import json
import re
import stat
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPO = '4uva4ek/galaxyxr-modules'
# Explicit public distribution allowlist. Source-only and local experiments
# cannot become downloadable merely because somebody creates a release tag.
MODULES = {
    'haptic-main': 'gxr_haptic_main',
    'controller-extrapolation': 'gxr_controller_extrapolation',
    'openxr-velocity-frame': 'gxr_openxr_velocity_frame',
    'controller-whitening': 'gxr_controller_whitening',
    'controller-timestamp-precision': 'gxr_controller_timestamp_precision',
}


def validate(archive, module, version):
    if module not in MODULES or not re.fullmatch(r'\d+\.\d+\.\d+', version):
        raise ValueError('Not an approved stable module/version')
    if archive.name != f'gxr-{module}-{version}-auto.zip':
        raise ValueError('Unexpected asset name; only automatic ZIPs are supported')
    with zipfile.ZipFile(archive) as z:
        entries = z.infolist()
        names = [i.filename for i in entries]
        if len(names) != len(set(names)) or len(names) > 128:
            raise ValueError('Duplicate entries or excessive file count')
        if sum(i.file_size for i in entries) > 64 * 1024 * 1024:
            raise ValueError('Package too large')
        for i in entries:
            if not re.fullmatch(r'[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*', i.filename):
                raise ValueError('Unsafe archive path')
            if any(p in ('.', '..') for p in i.filename.split('/')):
                raise ValueError('Unsafe archive path')
            mode = i.external_attr >> 16
            if stat.S_IFMT(mode) not in (0, stat.S_IFREG):
                raise ValueError('Non-regular archive entry')
        data = {n: z.read(n) for n in names}
    if data.get('disable') != b'' or data.get('skip_mount') != b'':
        raise ValueError('Missing empty disable/skip_mount marker')
    if any(n in data for n in ('post-fs-data.sh', 'post-mount.sh', 'system.prop', 'sepolicy.rule')):
        raise ValueError('Unexpected system/early-boot payload')
    props = {}
    for line in data['module.prop'].decode('utf-8').splitlines():
        if '=' in line:
            k, v = line.split('=', 1)
            if k in props:
                raise ValueError('Duplicate property')
            props[k] = v
    if props['id'] != MODULES[module] or props['version'] != version:
        raise ValueError('Release tag/package identity mismatch')
    code = int(props['versionCode'])
    if not 0 < code <= 2147483647:
        raise ValueError('Invalid versionCode')
    sums = {}
    for line in data['SHA256SUMS'].decode('ascii').splitlines():
        digest, name = line.split('  ', 1)
        if name in sums or not re.fullmatch(r'[0-9a-f]{64}', digest):
            raise ValueError('Invalid checksum manifest')
        sums[name] = digest
    expected = {n: hashlib.sha256(b).hexdigest() for n, b in data.items()
                if n not in ('disable', 'SHA256SUMS')}
    if sums != expected:
        raise ValueError('Incomplete or incorrect package checksums')
    return code


def promote(archive, tag, root=ROOT):
    module, version = tag.split('/v')
    code = validate(archive, module, version)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    sidecar = archive.with_suffix('.zip.sha256').read_text('ascii').strip()
    if sidecar != f'{digest}  {archive.name}':
        raise ValueError('Release asset SHA-256 mismatch')
    feed = {
        'version': version, 'versionCode': code,
        'zipUrl': f'https://github.com/{REPO}/releases/download/{tag}/{archive.name}',
        'changelog': f'https://raw.githubusercontent.com/{REPO}/main/changelogs/{module}.md',
        'sha256': digest,
    }
    dest = root / 'updates' / f'{module}.json'
    if dest.exists():
        old = json.loads(dest.read_text('utf-8'))
        if old == feed:
            return feed
        if code <= old['versionCode']:
            raise ValueError('Stable versionCode must increase; same-version assets are immutable')
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(json.dumps(feed, indent=2) + '\n', encoding='utf-8')
    return feed


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zip', type=Path, required=True)
    parser.add_argument('--tag', required=True)
    args = parser.parse_args()
    print(json.dumps(promote(args.zip, args.tag), indent=2))
