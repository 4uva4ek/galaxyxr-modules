"""Checks for tools/verify_built.py against synthetic dist folders."""
import hashlib
import tempfile
import unittest
import zipfile
from pathlib import Path

from verify_built import verify


def make_zip(root, module='haptic-main', overrides=None):
    folder = root/'dist'/module
    folder.mkdir(parents=True)
    archive = folder/f'gxr-{module}-1.0.0-auto.zip'
    data = {'module.prop': b'id=gxr_haptic_main\nversion=1.0.0\n', 'disable': b'',
            'service.sh': b'exit 0\n', 'bin/gxr-main': b'\x7fELF\x02\x01rest'}
    data.update(overrides or {})
    data = {name: blob for name, blob in data.items() if blob is not None}
    data['SHA256SUMS'] = ''.join(f'{hashlib.sha256(b).hexdigest()}  {n}\n'
                                 for n, b in sorted(data.items()) if n != 'disable').encode()
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
        for name, blob in sorted(data.items()):
            info = zipfile.ZipInfo(name, date_time=(2026, 10, 10, 0, 0, 0))
            info.external_attr = (0o100000 | (0o755 if name.endswith('.sh') or name.startswith('bin/') else 0o644)) << 16
            z.writestr(info, blob)
    archive.with_suffix('.zip.sha256').write_text(f'{hashlib.sha256(archive.read_bytes()).hexdigest()}  {archive.name}\n')
    return archive


class VerifyBuiltTests(unittest.TestCase):
    def test_accepts_a_complete_package(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = make_zip(root)
            self.assertEqual(verify('haptic-main', root), archive)

    def test_rejects_a_tampered_sidecar_or_archive(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = make_zip(root)
            archive.with_suffix('.zip.sha256').write_text('bad  digest\n')
            with self.assertRaises(ValueError):
                verify('haptic-main', root)

    def test_rejects_an_enabled_or_unhashed_package(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            make_zip(root, overrides={'disable': b'x'})
            with self.assertRaises(ValueError):
                verify('haptic-main', root)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = make_zip(root)
            with zipfile.ZipFile(archive, 'a') as z:
                z.writestr('extra.sh', b'exit 0\n')
            with self.assertRaises(ValueError):
                verify('haptic-main', root)

    def test_rejects_cr_bytes_and_wrong_modes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            make_zip(root, overrides={'service.sh': b'exit 0\r\n'})
            with self.assertRaises(ValueError):
                verify('haptic-main', root)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            make_zip(root)
            archive = root/'dist/haptic-main/gxr-haptic-main-1.0.0-auto.zip'
            data = {'module.prop': b'id=x\n', 'disable': b'', 'service.sh': b'exit 0\n'}
            data['SHA256SUMS'] = ''.join(f'{hashlib.sha256(b).hexdigest()}  {n}\n'
                                         for n, b in sorted(data.items()) if n != 'disable').encode()
            with zipfile.ZipFile(archive, 'w') as z:
                for name, blob in sorted(data.items()):
                    info = zipfile.ZipInfo(name)
                    info.external_attr = (0o100000 | 0o644) << 16
                    z.writestr(info, blob)
            archive.with_suffix('.zip.sha256').write_text(f'{hashlib.sha256(archive.read_bytes()).hexdigest()}  {archive.name}\n')
            with self.assertRaises(ValueError):
                verify('haptic-main', root)


if __name__ == '__main__':
    unittest.main()
