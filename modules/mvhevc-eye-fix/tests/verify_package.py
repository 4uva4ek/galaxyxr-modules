#!/usr/bin/env python3
"""Host-side replay of the GXR manager's package validation.

Mirrors userspace/ksud/src/gxr_update.rs in KernelSU-GXR (galaxyxr branch):
read_archive + validate_payload + install_notes + verify_installed, with the
installer's own error messages. A package that fails here would fail on the
headset, so never push a candidate that does not pass.

Usage: python tests/verify_package.py [ZIP]
Default ZIP: newest dist/mvhevc-eye-fix/*.zip. Exit 0 = the installer accepts it.
"""
import hashlib
import sys
import zipfile
from pathlib import Path
from typing import NoReturn

LIMIT = 64 * 1024 * 1024
INFO_MAX_BYTES = 4096
INFO_MAX_LINES = 32
INFO_MAX_LINE = 160
FORBIDDEN = {
    'post-fs-data.sh', 'post-mount.sh', 'system.prop', 'sepolicy.rule',
    'boot-completed.sh', 'remove', 'update', 'guard-boot-id', 'auto.pending', 'failure',
}


def fail(message) -> NoReturn:
    print(f'Error: {message}')
    raise SystemExit(1)


def safe_name(name):
    if not name:
        return False
    for part in name.split('/'):
        if not part or part in ('.', '..'):
            return False
        if not all(c.isascii() and (c.isalnum() or c in '._-') for c in part):
            return False
    return True


def main():
    root = Path(__file__).resolve().parents[3]
    if len(sys.argv) > 1:
        path = Path(sys.argv[1])
    else:
        zips = sorted((root / 'dist' / 'mvhevc-eye-fix').glob('*.zip'))
        path = zips[-1] if zips else None
    if not path or not path.exists():
        fail('package not found; build it first')
    print(f'validating {path.name}')
    # read_archive
    if path.stat().st_size > LIMIT:
        fail('ZIP too large')
    with zipfile.ZipFile(path) as archive:
        infos = archive.infolist()
        if len(infos) > 128:
            fail('Too many ZIP entries')
        files = {}
        total = 0
        for info in infos:
            name = info.filename
            kind = (info.external_attr >> 16) & 0o170000
            if not (safe_name(name) and kind in (0, 0o100000)):
                fail('Unsafe ZIP entry')
            total += info.file_size
            if total > LIMIT:
                fail('Unpacked ZIP too large')
            data = archive.read(name)
            if len(data) != info.file_size:
                fail('Invalid ZIP entry size')
            if name in files:
                fail('Duplicate ZIP entry')
            files[name] = data
    # validate_payload
    for marker in ('disable', 'skip_mount'):
        if files.get(marker) != b'':
            fail(f'Missing empty {marker}')
    props = {}
    for line in files.get('module.prop', b'').decode('utf-8').splitlines():
        if '=' in line:
            key, value = line.split('=', 1)
            if key in props:
                fail('Duplicate property')
            props[key] = value
    module_id = props.get('id', '')
    if not (module_id.startswith('gxr_') and len(module_id) > 4 and safe_name(module_id)):
        fail('Invalid GXR module ID')
    if 'metamodule' in props:
        fail('Metamodules are unsupported')
    try:
        version_code = int(props.get('versionCode', ''))
    except ValueError:
        fail('Missing versionCode')
    if version_code <= 0:
        fail('Invalid versionCode')
    for name in files:
        if not safe_name(name):
            fail('Unsafe payload path')
        if '/' in name and not (name.startswith('bin/') or name.startswith('webroot/')):
            fail('Unexpected payload directory')
        if name in FORBIDDEN:
            fail('Unexpected startup/state payload')
    sums = {}
    for line in files.get('SHA256SUMS', b'').decode('ascii').splitlines():
        if '  ' not in line:
            fail('Invalid SHA256SUMS line')
        digest, name = line.split('  ', 1)
        if not (safe_name(name) and len(digest) == 64 and all(c in '0123456789abcdefABCDEF' for c in digest)):
            fail('Invalid checksum entry')
        if name in sums:
            fail('Duplicate checksum')
        sums[name] = digest
    if len(sums) + 2 != len(files):
        fail('Incomplete checksum manifest')
    for name, digest in sums.items():
        if name in ('disable', 'SHA256SUMS'):
            fail('Invalid checksum manifest')
        if name not in files:
            fail('Missing payload')
        if hashlib.sha256(files[name]).hexdigest() != digest:
            fail(f'Checksum mismatch: {name}')
    if 'action.sh' not in files or 'service.sh' not in files:
        fail('Missing lifecycle scripts')
    if '--contract-only' not in sys.argv and any(name.startswith('bin/') for name in files):
        fail('package ships binary payloads; the patched libraries are generated on the device')
    # install_notes
    if 'install-info.txt' in files:
        raw = files['install-info.txt']
        if len(raw) > INFO_MAX_BYTES:
            fail('install-info.txt is too large')
        try:
            text = raw.decode('utf-8')
        except UnicodeDecodeError:
            fail('install-info.txt is not UTF-8')
        lines = text.splitlines()
        if len(lines) > INFO_MAX_LINES:
            fail('install-info.txt has too many lines')
        for line in lines:
            line = line.rstrip()
            if len(line) > INFO_MAX_LINE:
                fail('install-info.txt line too long')
            if any(ord(c) < 32 or ord(c) == 127 for c in line):
                fail('install-info.txt contains control characters')
    # verify_installed (runs on the staged tree before it is renamed into place)
    for required in ('action.sh', 'auto.sh', 'module.prop'):
        if required not in sums:
            fail('Unverified installed lifecycle script')
    print('OK: the GXR installer would accept this package (gxr_update.rs replay)')


if __name__ == '__main__':
    main()
