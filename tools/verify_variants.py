"""Check the manual and automatic release artifacts for one module."""
import hashlib
import json
import zipfile


def verify(module):
    root = module.parents[1]
    config = json.loads((module/'build.json').read_text())
    props = dict(line.split('=', 1) for line in (module/'module/module.prop').read_text().splitlines() if '=' in line)
    binaries = []
    for variant in ('manual', 'auto'):
        archive = root/'dist'/module.name/f'{config["artifact"]}-{props["version"]}-{variant}.zip'
        assert archive.with_suffix('.zip.sha256').read_text().split()[0] == hashlib.sha256(archive.read_bytes()).hexdigest()
        with zipfile.ZipFile(archive) as z:
            assert z.testzip() is None
            names = set(z.namelist())
            expected = {'action.sh', 'customize.sh', 'module.prop', 'skip_mount',
                        'bin/'+config['binary'], 'SHA256SUMS', 'NOTICE.txt', 'NDK-NOTICE.txt'}
            if variant == 'auto':
                expected |= {'auto.sh', 'autostart.sh', 'service.sh', 'auto-default'}
                assert z.read('auto-default') == b''
                assert b'auto.sh" resume' in z.read('service.sh')
            assert names == expected, names ^ expected
            assert z.read('skip_mount') == b''
            covered = set()
            for line in z.read('SHA256SUMS').decode().splitlines():
                digest, name = line.split('  ', 1)
                assert hashlib.sha256(z.read(name)).hexdigest() == digest, name
                covered.add(name)
            assert covered == names - {'SHA256SUMS'}
            binary = z.read('bin/'+config['binary'])
            assert binary[:6] == b'\x7fELF\x02\x01' and binary[18:20] == b'\xb7\x00'
            binaries.append(binary)
            for name in names:
                if name.endswith('.sh'):
                    assert b'\r' not in z.read(name)
            packaged = dict(line.split('=', 1) for line in z.read('module.prop').decode().splitlines() if '=' in line)
            assert packaged['id'] == props['id']
            assert packaged['name'].endswith(f'({variant})')
    assert binaries[0] == binaries[1], 'Variants must use the same native patcher'
    print(f'{module.name}: manual/auto contents, hashes, ARM64, same native patcher, no early-boot hooks: OK')
