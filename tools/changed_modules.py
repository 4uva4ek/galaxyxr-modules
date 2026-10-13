"""Plan which module ZIPs a change requires, one matrix entry per module.

Usage:
    python tools/changed_modules.py --base SHA --head SHA
    python tools/changed_modules.py --all
    python tools/changed_modules.py --files tools/cold-boot-reset.sh

A module is planned when a file under `modules/NAME/` changed or when a shared
`tools/` file its package embeds changed. Excluded modules are reported with
their reason and never built silently. Inside GitHub Actions it writes the
`matrix` and `count` outputs plus a step summary.
"""
import argparse
import json
import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NATIVE_BUILDER = 'tools/build.py'
# Never built by CI: deprecated, or needing proprietary inputs absent from the
# repository. The reason is printed in the run summary. See docs/RELEASING.md.
EXCLUDED = {
    'framebuffer10': 'deprecated 2026-10-13; pinned vendor payload not in repo; RELEASING.md says do not build',
    'controller-extrapolation': 'needs the compatibility header generated from a user-supplied libopenxr_android.so (proprietary, not in repo)',
    'openxr-velocity-frame': 'needs a user-supplied libopenxr_android.so (proprietary, not in repo)',
    'steamlink-pose-time': 'needs user-supplied libopenxr_android.so and libvrlink_scene.so (proprietary, not in repo)',
}


def props(module):
    text = (module/'module/module.prop').read_text('utf-8')
    return dict(line.split('=', 1) for line in text.splitlines() if '=' in line)


def registry(root=ROOT):
    """Map module name to its build entry, its exclusion, or None when unknown."""
    entries = {}
    for source in sorted((root/'modules').iterdir()):
        if not source.is_dir():
            continue
        name = source.name
        version = props(source)['version']
        if name in EXCLUDED:
            entries[name] = {'excluded': EXCLUDED[name]}
        elif (source/'build.json').is_file():
            config = json.loads((source/'build.json').read_text('utf-8'))
            shared = set(config.get('shared_files', [])) | set(config.get('auto_shared_files', []))
            entries[name] = {
                'artifact': f'{config["artifact"]}-{version}-auto',
                'command': f'python3 {NATIVE_BUILDER} {name} --ndk "$ANDROID_NDK_HOME" --variant auto',
                'needs_ndk': True,
                'inputs': {NATIVE_BUILDER} | {f'tools/{f}' for f in shared},
            }
        elif (source/'build.py').is_file():
            entries[name] = {
                'artifact': f'gxr-{name}-{version}-auto',
                'command': f'python3 modules/{name}/build.py',
                'needs_ndk': False,
                'inputs': set(),
            }
        else:
            entries[name] = None
    return entries


def plan(changed, entries):
    """Return (builds, excluded, unchanged) for a set of changed repository paths."""
    changed = set(changed)
    touched = lambda prefix: any(path.startswith(prefix) for path in changed)
    builds, excluded, unchanged = [], [], []
    for name, entry in sorted(entries.items()):
        if entry is None or 'excluded' in entry:
            if touched(f'modules/{name}/'):
                reason = 'no CI build entry' if entry is None else entry['excluded']
                excluded.append({'module': name, 'reason': reason})
            continue
        inputs = entry['inputs'] & changed
        own = touched(f'modules/{name}/')
        if own or inputs:
            builds.append({'module': name, 'artifact': entry['artifact'], 'command': entry['command'],
                           'needs_ndk': entry['needs_ndk'],
                           'why': ', '.join(sorted(inputs) + ([f'modules/{name}/'] if own else []))})
        else:
            unchanged.append(name)
    return builds, excluded, unchanged


def changed_files(base, head):
    """Changed names between a readable base commit and head, or None if unknown."""
    if not base or set(base) == {'0'}:
        return None
    probe = subprocess.run(['git', '-C', str(ROOT), 'cat-file', '-e', f'{base}^{{commit}}'],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if probe.returncode:
        return None
    names = subprocess.check_output(['git', '-C', str(ROOT), 'diff', '--name-only', f'{base}...{head}'], text=True)
    return names.splitlines()


def build_all(entries, why='full build'):
    builds = [{'module': name, 'artifact': entry['artifact'], 'command': entry['command'],
               'needs_ndk': entry['needs_ndk'], 'why': why}
              for name, entry in sorted(entries.items()) if entry and 'excluded' not in entry]
    excluded = [{'module': name, 'reason': entry['excluded']} for name, entry in sorted(entries.items())
                if entry and 'excluded' in entry]
    return builds, excluded, []


def summary(builds, excluded, unchanged, note):
    lines = [f'## Module build plan ({note})', '']
    lines.append('Building: ' + (', '.join(f'`{b["module"]}`' for b in builds) if builds else 'nothing'))
    for item in excluded:
        lines.append(f'Skipped `{item["module"]}`: {item["reason"]}')
    if unchanged:
        lines.append(f'Unchanged: {len(unchanged)} module(s)')
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', help='Base commit of the change (may be a zero SHA on branch creation)')
    parser.add_argument('--head', default='HEAD')
    parser.add_argument('--files', nargs='*', help='Plan for an explicit file list instead of a Git range')
    parser.add_argument('--all', action='store_true', help='Build every module CI can build')
    args = parser.parse_args()
    entries = registry()
    if args.all:
        builds, excluded, unchanged = build_all(entries, 'manual full build')
        note = 'manual full build'
    else:
        files = args.files if args.files is not None else changed_files(args.base, args.head)
        if files is None:
            builds, excluded, unchanged = build_all(entries, 'full build (unknown base)')
            note = 'base commit unknown (new branch or force push), full build'
        else:
            builds, excluded, unchanged = plan(files, entries)
            note = f'{len(files)} changed file(s)'
    matrix = {'include': builds}
    output = os.environ.get('GITHUB_OUTPUT')
    if output:
        with open(output, 'a', encoding='utf-8', newline='\n') as handle:
            handle.write(f'matrix={json.dumps(matrix)}\ncount={len(builds)}\n')
    step_summary = os.environ.get('GITHUB_STEP_SUMMARY')
    if step_summary:
        with open(step_summary, 'a', encoding='utf-8', newline='\n') as handle:
            handle.write(summary(builds, excluded, unchanged, note))
    print(f'Plan ({note}): {len(builds)} build(s), {len(excluded)} excluded, {len(unchanged)} unchanged')
    if not output:
        print(json.dumps(matrix, indent=2))


if __name__ == '__main__':
    main()
