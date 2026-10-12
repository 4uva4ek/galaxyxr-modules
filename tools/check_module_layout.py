"""Validate local module metadata, including unpublished and ignored experiments.

This check neither requires a published feed nor changes the public catalogue.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
AUTHOR = '4uva4ek'
FEED_BASE = 'https://raw.githubusercontent.com/4uva4ek/galaxyxr-modules/main/updates/'


def check(root=ROOT, module=None):
    if module is not None and not re.fullmatch(r'[a-z0-9-]+', module):
        raise ValueError('Invalid module name')
    sources = sorted((root / 'modules').glob(f'{module or chr(42)}/module/module.prop'))
    if not sources:
        raise ValueError('No module sources found')
    identities = set()
    for source in sources:
        name = source.parents[1].name
        raw = source.read_bytes()
        if b'\r' in raw:
            raise ValueError(f'{name}: module.prop must use LF')
        props = {}
        for line in raw.decode('utf-8').splitlines():
            if not line.strip() or line.startswith('#'):
                continue
            key, sep, value = line.partition('=')
            if not sep or key in props or not value.strip():
                raise ValueError(f'{name}: malformed or duplicate property: {key}')
            props[key] = value
        for key in ('id', 'name', 'version', 'versionCode', 'author', 'description', 'updateJson'):
            if key not in props:
                raise ValueError(f'{name}: missing {key}')
        if not re.fullmatch(r'gxr_[a-z0-9_]+', props['id']) or props['id'] in identities:
            raise ValueError(f'{name}: invalid or duplicate ID')
        identities.add(props['id'])
        if not props['versionCode'].isdigit() or int(props['versionCode']) <= 0:
            raise ValueError(f'{name}: invalid versionCode')
        if props['author'] != AUTHOR:
            raise ValueError(f'{name}: author must be {AUTHOR}')
        if props['updateJson'] != f'{FEED_BASE}{name}.json':
            raise ValueError(f'{name}: noncanonical updateJson')
        for required in (source.parents[1] / 'README.md', root / 'changelogs' / f'{name}.md'):
            if not required.is_file():
                raise ValueError(f'{name}: missing {required.name}')
    print(f'Module layout: {len(sources)} local module folders verified; author={AUTHOR}')


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--module', help='Validate one module without changing legacy source metadata')
    check(module=parser.parse_args().module)
