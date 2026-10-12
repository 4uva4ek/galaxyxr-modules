"""Check the repository source catalogue without requiring native build inputs."""
import json
from update_feed import MODULES, REPO, ROOT

def check():
    for name, module_id in MODULES.items():
        source = ROOT / 'modules' / name / 'module/module.prop'
        props = dict(line.split('=', 1) for line in source.read_text('utf-8').splitlines() if '=' in line)
        assert props['id'] == module_id, f'{name}: ID mismatch'
        assert props['updateJson'] == f'https://raw.githubusercontent.com/{REPO}/main/updates/{name}.json'
        feed = json.loads((ROOT / 'updates' / f'{name}.json').read_text('utf-8'))
        assert 0 < feed['versionCode'] <= int(props['versionCode']), f'{name}: feed/source version mismatch'
        version = feed['version']
        assert feed['zipUrl'] == f'https://github.com/{REPO}/releases/download/{name}/v{version}/gxr-{name}-{version}-auto.zip'
        assert feed['changelog'] == f'https://raw.githubusercontent.com/{REPO}/main/changelogs/{name}.md'
        assert (ROOT / 'changelogs' / f'{name}.md').is_file()
    print(f'Catalogue: {len(MODULES)} independent public modules verified')

if __name__ == '__main__':
    check()
