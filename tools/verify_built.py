"""Verify a built module ZIP under dist/: sidecar digest, inner hashes, modes.

    python tools/verify_built.py haptic-main

Requires no NDK and no device; mirrors the packaging rules in docs/RELEASING.md.
"""
import argparse
import hashlib
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def verify(name, root=ROOT):
    output = root/'dist'/name
    archives = sorted(output.glob('*-auto.zip'))
    if len(archives) != 1:
        raise ValueError(f'{name}: expected one *-auto.zip in dist/{name}, found {len(archives)}')
    archive = archives[0]
    sidecar = Path(str(archive) + '.sha256')
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    recorded = sidecar.read_text('ascii').split()[0]
    if recorded != digest:
        raise ValueError(f'{name}: sidecar digest does not match {archive.name}')
    with zipfile.ZipFile(archive) as z:
        if z.testzip() is not None:
            raise ValueError(f'{name}: corrupt archive')
        names = set(z.namelist())
        for required in ('module.prop', 'SHA256SUMS', 'disable'):
            if required not in names:
                raise ValueError(f'{name}: missing {required}')
        if z.read('disable') != b'':
            raise ValueError(f'{name}: disable marker must be empty')
        covered = set()
        for line in z.read('SHA256SUMS').decode('ascii').splitlines():
            recorded_hash, sep, member = line.partition('  ')
            if not sep or hashlib.sha256(z.read(member)).hexdigest() != recorded_hash:
                raise ValueError(f'{name}: bad hash for {member}')
            covered.add(member)
        if covered != names - {'SHA256SUMS', 'disable'}:
            raise ValueError(f'{name}: SHA256SUMS does not cover every payload file')
        for member in sorted(names):
            if member.endswith('.sh') and b'\r' in z.read(member):
                raise ValueError(f'{name}: CR bytes in {member}')
            mode = z.getinfo(member).external_attr >> 16 & 0o777
            if (member.endswith('.sh') or member.startswith('bin/')) and mode != 0o755:
                raise ValueError(f'{name}: {member} mode {mode:o}, expected 755')
    print(f'{name}: {archive.name}, {archive.stat().st_size} bytes, {len(names)} files, hashes and modes OK')
    return archive


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('module')
    verify(parser.parse_args().module)
