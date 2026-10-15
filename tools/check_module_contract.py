"""Module contract checks for every tracked module source, published or not.

Runs on every branch and pull request; needs no NDK, network or device.

- The repository root holds only the documented entries.
- modules/NAME/module/ ships the GXR package contract: skip_mount (in the source
  tree or produced by the module's own builder), action.sh, auto.sh, service.sh
  and a customize.sh that aborts the generic installer path, and no startup or
  state payload that the GXR installer rejects.
- Every module shell script uses the Android shebang (customize.sh is sourced
  by the installer and carries none), LF endings and passes bash -n.
- The module reports the universal ENABLED/DISABLED labels.
- uninstall.sh does not call Android services, directly or through a script it
  runs synchronously: KernelSU executes it in post-fs-data, before pm, settings,
  cmd or am exist.
- Shell-only modules (build.py without build.json or native/) are built; their
  ZIP must pass the stable-feed validator and the release pipeline's package
  checks, and their host tests must pass.
"""
import json
import os
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import update_feed  # noqa: E402

ROOT_ENTRIES = {'.gitattributes', '.github', '.gitignore', 'AGENTS.md', 'README.md', 'modules.json',
                'changelogs', 'docs', 'modules', 'templates', 'tools', 'updates'}
LIFECYCLE = {'action.sh', 'auto.sh', 'service.sh', 'customize.sh'}
REJECTED = {'post-fs-data.sh', 'post-mount.sh', 'system.prop', 'sepolicy.rule', 'boot-completed.sh',
            'remove', 'update', 'guard-boot-id', 'auto.pending', 'failure', 'disable'}
NATIVE = {'.c', '.cpp', '.h', '.hpp', '.inc', '.S'}
# Android services in command position, by absolute path, or through the
# variables the modules use for them.
SERVICE_CALL = re.compile(r'(?:(?:^|[;&|(`]|\$\()\s*(?:/system/bin/)?(?:pm|settings|cmd|am)\s'
                          r'|/system/bin/(?:pm|settings|cmd|am)\b|"\$(?:PM|SETTINGS)")')
SYNC_SCRIPT = re.compile(r'sh\s+"\$MODDIR/([A-Za-z0-9_.-]+\.sh)"[^&]*$')
GIT_BASH = Path('C:/Program Files/Git/bin/bash.exe')
BASH = str(GIT_BASH) if os.name == 'nt' and GIT_BASH.is_file() else shutil.which('bash')


def tracked():
    out = subprocess.run(['git', 'ls-files', '-z'], cwd=ROOT, capture_output=True, text=True, check=True).stdout
    return [ROOT / p for p in out.split('\0') if p]


def code_lines(text):
    for number, line in enumerate(text.splitlines(), 1):
        code = line.split('#', 1)[0]
        if code.strip():
            yield number, code


def shell_ok(label, data, problems, sourced=False):
    if not sourced and not data.startswith(b'#!/system/bin/sh\n'):
        problems.append(f'{label}: first line must be #!/system/bin/sh')
    if b'\r' in data:
        problems.append(f'{label}: CRLF line endings')
    result = subprocess.run([BASH, '-n'], input=data, capture_output=True)
    if result.returncode:
        problems.append(f'{label}: syntax: {result.stderr.decode(errors="replace").strip()}')


def service_calls(source, script, problems, depth=0):
    path = source / script
    if not path.is_file():
        return
    for number, code in code_lines(path.read_text('utf-8', 'replace')):
        if SERVICE_CALL.search(code):
            problems.append(f'{source.parent.name}: module/{script}:{number} reaches an Android service from the '
                            f'removal path; KernelSU runs uninstall.sh in post-fs-data where pm/settings/cmd/am do not exist')
        for called in SYNC_SCRIPT.findall(code):
            if depth < 2 and called != script:
                service_calls(source, called, problems, depth + 1)


def check_module(name, files, problems):
    module = ROOT / 'modules' / name
    source = module / 'module'
    own = [f for f in files if source in f.parents]
    names = {f.relative_to(source).as_posix() for f in own}
    for missing in sorted(LIFECYCLE - names):
        problems.append(f'{name}: module/{missing} is missing')
    for rejected in sorted(names & REJECTED):
        problems.append(f'{name}: module/{rejected} is a startup/state payload the GXR installer rejects')
    config = json.loads((module / 'build.json').read_text('utf-8')) if (module / 'build.json').is_file() else None
    builders = [p for p in (module / 'build.py', module / 'build_module.py') if p.is_file()]
    if config is not None:
        if 'skip_mount' not in config.get('files', []):
            problems.append(f'{name}: build.json files must include skip_mount')
    elif 'skip_mount' not in names and not any('skip_mount' in p.read_text('utf-8') for p in builders):
        problems.append(f'{name}: no skip_mount marker in module/ and no builder that adds it')
    if 'customize.sh' in names:
        statements = [code.strip() for _, code in code_lines((source / 'customize.sh').read_text('utf-8'))
                      if not code.startswith('#!')]
        if not statements or not statements[0].startswith('abort'):
            problems.append(f'{name}: customize.sh must abort the generic installer path first')
    corpus = []
    for f in own:
        if f.suffix == '.sh':
            data = f.read_bytes()
            shell_ok(f.relative_to(ROOT).as_posix(), data, problems, sourced=f.name == 'customize.sh')
            corpus.append(data.decode('utf-8', 'replace'))
    corpus += [f.read_text('utf-8', 'replace') for f in files if (module / 'native') in f.parents and f.suffix in NATIVE]
    if config is not None:
        corpus += [(ROOT / 'tools' / s).read_text('utf-8', 'replace') for s in
                   config.get('shared_files', []) + config.get('auto_shared_files', []) if (ROOT / 'tools' / s).is_file()]
    text = ''.join(corpus)
    for shared in set(re.findall(r'\b([a-z0-9-]+\.sh)\b', text)):
        if (ROOT / 'tools' / shared).is_file():
            text += (ROOT / 'tools' / shared).read_text('utf-8', 'replace')
    for label in ('ENABLED', 'DISABLED'):
        if label not in text:
            problems.append(f'{name}: the module never reports {label}; see docs/ACTION-REPORT.md')
    service_calls(source, 'uninstall.sh', problems)
    if (module / 'build.py').is_file() and config is None and not (module / 'native').is_dir():
        check_package(name, module, problems)


def check_package(name, module, problems):
    props = dict(line.split('=', 1) for line in (module / 'module/module.prop').read_text('utf-8').splitlines() if '=' in line)
    if subprocess.run([sys.executable, str(module / 'build.py')], cwd=ROOT).returncode:
        problems.append(f'{name}: build.py failed')
        return
    archive = ROOT / 'dist' / name / f'gxr-{name}-{props["version"]}-auto.zip'
    approved = dict(update_feed.MODULES)
    update_feed.MODULES[name] = props['id']  # the contract applies before catalogue approval
    try:
        update_feed.validate(archive, name, props['version'])
    except Exception as error:  # noqa: BLE001 - report every rejection reason
        problems.append(f'{name}: {archive.name} rejected by the feed validator: {error}')
        return
    finally:
        update_feed.MODULES.clear()
        update_feed.MODULES.update(approved)
    with zipfile.ZipFile(archive) as z:
        names = set(z.namelist())
        for missing in sorted(LIFECYCLE - names):
            problems.append(f'{name}: {archive.name} lacks {missing}; the release pipeline rejects it')
        packaged = dict(line.split('=', 1) for line in z.read('module.prop').decode().splitlines() if '=' in line)
        if packaged.get('updateJson') != props['updateJson']:
            problems.append(f'{name}: packaged module.prop updateJson differs from the source')
        for info in z.infolist():
            if info.filename.endswith('.sh'):
                if info.external_attr >> 16 & 0o777 != 0o755:
                    problems.append(f'{name}: {archive.name}:{info.filename} is not mode 0755')
                shell_ok(f'{archive.name}:{info.filename}', z.read(info), problems, sourced=info.filename == 'customize.sh')
    for test in sorted(list(module.glob('test_*.py')) + list((module / 'tests').glob('test_*.py'))):
        if subprocess.run([sys.executable, str(test)], cwd=ROOT).returncode:
            problems.append(f'{name}: {test.relative_to(module).as_posix()} failed')


def main():
    files = tracked()
    problems = []
    for entry in sorted({f.relative_to(ROOT).parts[0] for f in files} - ROOT_ENTRIES):
        problems.append(f'repository root: unexpected entry {entry}; see docs/MODULE-LAYOUT.md')
    modules = sorted({f.parent.parent.name for f in files
                      if f.name == 'module.prop' and f.parent.name == 'module' and f.parent.parent.parent == ROOT / 'modules'})
    for name in modules:
        check_module(name, files, problems)
    if problems:
        print('\n'.join(problems))
        raise SystemExit(f'Module contract: {len(problems)} problem(s) in {len(modules)} tracked modules')
    print(f'Module contract: {len(modules)} tracked modules verified')


if __name__ == '__main__':
    main()
