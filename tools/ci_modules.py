"""Build/release approved modules; version bumps are the publication signal."""
import argparse
import base64
import hashlib
import json
import lzma
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import zipfile

from update_feed import MODULES, ROOT, REPO, validate, promote

NDK_VERSION = '28.2.13676358'
RUNTIME_MODULES = {'controller-extrapolation', 'openxr-velocity-frame', 'steamlink-pose-time'}
INPUTS = {
    'runtime': ('GXR_RUNTIME_', 18, 'libopenxr_android.so', 'a7dabaa1030cc7979efe987f212488c61b61e8afb9aa3add59f7ae4fbeff4c92'),
    'scene': ('GXR_SCENE_', 16, 'libvrlink_scene.so', 'a1f1907ee91dc30dcdb4e3924cf3cff6e78302131fa835d9eb40f3fd4946ebde'),
}


def run(*args, capture=False, check=True):
    result = subprocess.run([str(a) for a in args], cwd=ROOT, text=True, capture_output=capture, check=check)
    return result.stdout.strip() if capture else result


def properties(text):
    return dict(line.split('=', 1) for line in text.splitlines() if '=' in line and not line.startswith('#'))


def source_props(name):
    return properties((ROOT/'modules'/name/'module/module.prop').read_text('utf-8'))


def version_tuple(value):
    if not re.fullmatch(r'\d+\.\d+\.\d+', value):
        raise ValueError('Public version must have three numeric components')
    return tuple(map(int, value.split('.')))


def bumped(old, new):
    if old is not None and (old['version'], old['versionCode']) == (new['version'], new['versionCode']):
        return False
    new_version = version_tuple(new['version'])
    if not new['versionCode'].isdigit() or not 0 < int(new['versionCode']) <= 2147483647:
        raise ValueError('Invalid versionCode')
    if old is not None and (new_version <= version_tuple(old['version']) or int(new['versionCode']) <= int(old['versionCode'])):
        raise ValueError('Increase BOTH version and versionCode for a release')
    return True


def pending(name):
    p = source_props(name)
    feed_path = ROOT/'updates'/f'{name}.json'
    return not feed_path.exists() or bumped(
        {k: str(v) for k, v in json.loads(feed_path.read_text()).items()}, p)


def plan(before=None, selection='all', publish=False):
    if selection != 'all' and selection not in MODULES:
        raise ValueError('Module is not in the approved public catalogue')
    selected = sorted(MODULES) if selection == 'all' else [selection]
    if before:
        if not re.fullmatch(r'[0-9a-f]{40}', before) or before == '0'*40:
            raise ValueError('Cannot establish the previous commit; use an explicit dispatch')
        run('git', 'cat-file', '-e', before+'^{commit}')
        changed = []
        for name in selected:
            previous = run('git', 'show', f'{before}:modules/{name}/module/module.prop', capture=True, check=False)
            if bumped(properties(previous) if previous else None, source_props(name)):
                if pending(name):
                    changed.append(name)
        selected = changed
    elif publish:
        selected = [name for name in selected if pending(name)]
    return selected


def restore_input(kind, destination):
    prefix, count, filename, expected = INPUTS[kind]
    chunks = [os.environ.get(f'{prefix}{index:02}', '') for index in range(1, count+1)]
    if not all(chunks):
        raise ValueError(f'Missing private {kind} build input; no fallback or hash bypass')
    compressed = base64.b64decode(''.join(chunks), validate=True)
    decoder = lzma.LZMADecompressor()
    data = decoder.decompress(compressed, max_length=8*1024*1024)
    if not decoder.eof or decoder.unused_data or hashlib.sha256(data).hexdigest() != expected:
        raise ValueError(f'Private {kind} input integrity mismatch')
    destination.mkdir(parents=True, exist_ok=True)
    path = destination/filename
    path.write_bytes(data)
    path.chmod(0o600)
    print(f'Pinned {kind} input verified')


def build(names, ndk, inputs):
    run(sys.executable, 'tools/check_module_layout.py')
    run(sys.executable, 'tools/check_catalogue.py')
    run(sys.executable, 'tools/test_update_feed.py')
    run(sys.executable, 'tools/test_ci_modules.py')
    runtime = inputs/INPUTS['runtime'][2]
    scene = inputs/INPUTS['scene'][2]
    manifest = {'commit': run('git', 'rev-parse', 'HEAD', capture=True), 'modules': {}}
    for name in names:
        if name not in MODULES:
            raise ValueError('Unapproved module')
        if name == 'openxr-velocity-frame':
            run(sys.executable, f'modules/{name}/build_module.py', '--ndk', ndk, '--runtime', runtime)
            for test in ('verify.py', 'test_compatibility.py'):
                run(sys.executable, f'modules/{name}/{test}', '--ndk', ndk)
            run(sys.executable, f'modules/{name}/test_pre_prediction.py', '--runtime', runtime)
            run(sys.executable, f'modules/{name}/verify_package.py')
        elif name == 'steamlink-pose-time':
            run(sys.executable, f'modules/{name}/build.py', '--ndk', ndk, '--runtime', runtime, '--scene', scene)
        else:
            if name == 'controller-extrapolation':
                run(sys.executable, 'modules/openxr-velocity-frame/prepare.py', '--ndk', ndk, '--runtime', runtime)
            run(sys.executable, 'tools/build.py', name, '--ndk', ndk)
            package_test = ROOT/'modules'/name/'tests/verify_package.py'
            if package_test.exists():
                run(sys.executable, package_test)
        if name == 'openxr-quality':
            for test in ('test_update_contract.py', 'test_gate.py'):
                run(sys.executable, f'modules/{name}/tests/{test}')
        props = source_props(name)
        version = props['version']
        archive = ROOT/'dist'/name/f'gxr-{name}-{version}-auto.zip'
        code = validate(archive, name, version)
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        if archive.with_suffix('.zip.sha256').read_text().strip() != f'{digest}  {archive.name}':
            raise ValueError('Sidecar mismatch')
        with zipfile.ZipFile(archive) as z:
            if z.testzip() is not None:
                raise ValueError('Corrupt ZIP')
            packaged = properties(z.read('module.prop').decode())
            if packaged.get('author') != '4uva4ek' or packaged.get('updateJson') != props['updateJson']:
                raise ValueError('Package metadata mismatch')
            if not {'action.sh', 'auto.sh', 'service.sh', 'customize.sh'} <= set(z.namelist()):
                raise ValueError('Missing reviewed lifecycle contract')
            for entry in z.infolist():
                data = z.read(entry)
                if entry.filename.startswith('bin/'):
                    if data[:6] != b'\x7fELF\x02\x01' or data[18:20] != b'\xb7\x00':
                        raise ValueError('Non-ARM64 payload')
                if entry.filename.endswith('.sh'):
                    if b'\r' in data or entry.external_attr >> 16 & 0o777 != 0o755:
                        raise ValueError('Invalid shell payload')
                    result = subprocess.run(['bash', '-n'], input=data, capture_output=True)
                    if result.returncode:
                        raise ValueError('Shell syntax failure: '+entry.filename)
        manifest['modules'][name] = {'version': version, 'versionCode': code, 'sha256': digest,
                                    'archive': str(archive.relative_to(ROOT)), 'tag': f'{name}/v{version}'}
    (ROOT/'dist/ci-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print('Validated modules: '+', '.join(names))


def release_info(tag):
    result = subprocess.run(['gh','api',f'repos/{REPO}/releases/tags/{tag}'],cwd=ROOT,text=True,capture_output=True)
    if result.returncode == 0:
        return json.loads(result.stdout)
    if '404' in result.stderr:
        return None
    raise RuntimeError('Cannot query release; refusing to assume it is absent')


def verify_download(item):
    archive = ROOT/item['archive']
    with tempfile.TemporaryDirectory() as temp:
        run('gh','release','download',item['tag'],'--repo',REPO,'--dir',temp,'--pattern',archive.name,'--pattern',archive.name+'.sha256')
        if hashlib.sha256((Path(temp)/archive.name).read_bytes()).hexdigest() != item['sha256']:
            raise ValueError('Published assets differ; never overwrite an existing version')
        if (Path(temp)/(archive.name+'.sha256')).read_text().strip() != archive.with_suffix('.zip.sha256').read_text().strip():
            raise ValueError('Published checksum mismatch')


def publish(manifest_path):
    manifest = json.loads(manifest_path.read_text())
    commit = manifest['commit']
    if commit != os.environ['GITHUB_SHA'] or not re.fullmatch(r'[0-9a-f]{40}',commit):
        raise ValueError('Build/trigger commit mismatch')
    run('git','config','user.name','github-actions[bot]')
    run('git','config','user.email','41898282+github-actions[bot]@users.noreply.github.com')
    run('git','fetch','origin','main','--tags')
    run('git','merge-base','--is-ancestor',commit,'origin/main')
    for name,item in manifest['modules'].items():
        if name not in MODULES or item['tag'] != f'{name}/v{item["version"]}':
            raise ValueError('Invalid publication manifest')
        archive=ROOT/item['archive']
        if archive.resolve().parent != (ROOT/'dist'/name).resolve():
            raise ValueError('Unexpected archive location')
        if validate(archive,name,item['version']) != item['versionCode'] or hashlib.sha256(archive.read_bytes()).hexdigest()!=item['sha256']:
            raise ValueError('Build artifact changed')
        existing=release_info(item['tag'])
        tag_result=subprocess.run(['git','rev-parse','--verify',item['tag']+'^{commit}'],cwd=ROOT,text=True,capture_output=True)
        if tag_result.returncode==0:
            if tag_result.stdout.strip()!=commit:
                raise ValueError('Existing tag belongs to another commit; bump the version')
        else:
            run('git','tag','-a',item['tag'],'-m',item['tag'],commit)
            run('git','push','origin','refs/tags/'+item['tag'])
        if existing is None:
            notes=ROOT/'dist'/name/'release-notes.md'
            notes.write_text((ROOT/'changelogs'/f'{name}.md').read_text()+
                '\n\nAutomatic package, installed DISABLED. Enablement and soft restart are separate actions.\n'+
                f'\nBuilt from `{commit}` using Android NDK {NDK_VERSION}. CI verifies software/package checks; it is not a physical headset test.\n'+
                f'\nZIP SHA-256: `{item["sha256"]}`\n')
            run('gh','release','create',item['tag'],'--repo',REPO,'--verify-tag','--draft','--title',name+' '+item['version'],'--notes-file',notes)
            existing=release_info(item['tag'])
        if existing['draft']:
            assets={a['name']:a for a in existing['assets']}
            for file in (archive,archive.with_suffix('.zip.sha256')):
                if file.name not in assets:
                    run('gh','release','upload',item['tag'],file,'--repo',REPO)
            verify_download(item)
            run('gh','release','edit',item['tag'],'--repo',REPO,'--draft=false','--prerelease=false','--latest=false')
        else:
            if existing['prerelease']:
                raise ValueError('Existing prerelease requires explicit review')
            verify_download(item)
    # GITHUB_TOKEN releases do not trigger release workflows; promote here.
    # Only this job edits feeds, after every release has been verified public.
    run('git','switch','--detach','origin/main')
    for name,item in manifest['modules'].items():
        feed_path=ROOT/'updates'/f'{name}.json'
        if feed_path.exists() and json.loads(feed_path.read_text())['versionCode']>item['versionCode']:
            print(f'{name}: a newer feed already exists; not downgrading')
            continue
        promote(ROOT/item['archive'],item['tag'])
    run(sys.executable,'tools/check_catalogue.py')
    run('git','add','updates/')
    if not run('git','diff','--cached','--name-only',capture=True):
        return
    run('git','commit','-m','release: Publish verified automatic module feeds')
    for attempt in range(3):
        if run('git','push','origin','HEAD:main',check=False).returncode==0:
            return
        run('git','pull','--rebase','origin','main')
    raise RuntimeError('Feed push did not succeed; published immutable releases are retained')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest='command',required=True)
    p=sub.add_parser('plan');p.add_argument('--before');p.add_argument('--module',default='all');p.add_argument('--publish',action='store_true')
    p=sub.add_parser('restore-input');p.add_argument('kind',choices=INPUTS);p.add_argument('--directory',type=Path,required=True)
    p=sub.add_parser('build');p.add_argument('--modules',required=True);p.add_argument('--ndk',type=Path,required=True);p.add_argument('--inputs',type=Path,required=True)
    p=sub.add_parser('publish');p.add_argument('--manifest',type=Path,default=ROOT/'dist/ci-manifest.json')
    args=parser.parse_args()
    if args.command=='plan':
        names=plan(args.before,args.module,args.publish)
        result={'modules':json.dumps(names),'any':str(bool(names)).lower(),
                'runtime':str(bool(set(names)&RUNTIME_MODULES)).lower(),'scene':str('steamlink-pose-time' in names).lower()}
        if os.environ.get('GITHUB_OUTPUT'):
            with open(os.environ['GITHUB_OUTPUT'],'a') as f:
                for key,value in result.items(): f.write(f'{key}={value}\n')
        print(json.dumps(result))
    elif args.command=='restore-input':restore_input(args.kind,args.directory)
    elif args.command=='build':build(json.loads(args.modules),args.ndk,args.inputs)
    else:publish(args.manifest)


if __name__=='__main__':main()
