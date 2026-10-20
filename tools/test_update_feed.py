"""Checks for stable promotion: no device access or package execution."""
import hashlib
import json
import tempfile
import unittest
import zipfile
from unittest.mock import patch
from pathlib import Path
from update_feed import promote
import update_feed
import check_catalogue


class CatalogueTests(unittest.TestCase):
    def test_experimental_entries_are_not_stable_release_candidates(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            entries = [
                {'id': 'gxr_regular', 'updateJson': f'https://raw.githubusercontent.com/{update_feed.REPO}/main/updates/regular.json'},
                {'id': 'gxr_trial', 'experimental': True,
                 'updateJson': f'https://raw.githubusercontent.com/{update_feed.REPO}/main/updates/trial.json'},
            ]
            def save():
                (root / 'modules.json').write_text(json.dumps({'schemaVersion': 1, 'modules': entries}))
            save()
            with patch.object(update_feed, 'ROOT', root):
                self.assertEqual({'regular': 'gxr_regular'}, update_feed.load_registry())
                self.assertEqual({'regular': 'gxr_regular', 'trial': 'gxr_trial'},
                                 update_feed.load_registry(include_experimental=True))
                (root / 'modules-experimental.json').write_text(json.dumps({
                    'schemaVersion': 1, 'modules': [entries.pop()]}))
                save()
                self.assertEqual({'regular': 'gxr_regular'}, update_feed.load_registry())
                self.assertEqual({'regular': 'gxr_regular', 'trial': 'gxr_trial'},
                                 update_feed.load_registry(include_experimental=True))
                (root / 'modules-experimental.json').unlink()
                entries.append({'id': 'gxr_trial',
                    'updateJson': f'https://raw.githubusercontent.com/{update_feed.REPO}/main/updates/trial.json'})
                entries[1]['experimental'] = 'true'
                save()
                with self.assertRaises(ValueError):
                    update_feed.load_registry()

    def test_first_release_and_existing_feed_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'modules/first/module/module.prop'
            source.parent.mkdir(parents=True)
            source.write_text(
                'id=gxr_first\nversion=0.1.0\nversionCode=1\n'
                f'updateJson=https://raw.githubusercontent.com/{check_catalogue.REPO}/main/updates/first.json\n')
            (root / 'changelogs').mkdir()
            changelog = root / 'changelogs/first.md'
            with patch.object(check_catalogue, 'ROOT', root), \
                 patch.object(check_catalogue, 'MODULES', {'first': 'gxr_first'}):
                # Missing feeds must not bypass source/changelog checks.
                with self.assertRaises(AssertionError):
                    check_catalogue.check()
                changelog.write_text('## 0.1.0\nFirst release\n')
                check_catalogue.check()
                (root / 'updates').mkdir()
                feed = root / 'updates/first.json'
                feed.write_text('not json')
                with self.assertRaises(ValueError):
                    check_catalogue.check()
                feed.write_text('{"versionCode": 2}')
                with self.assertRaises(AssertionError):
                    check_catalogue.check()


class PromotionTests(unittest.TestCase):
    def make_zip(self, root, version='1.0.0', code=1, **overrides):
        path = root / f'gxr-haptic-main-{version}-auto.zip'
        data = {'module.prop': f'id=gxr_haptic_main\nversion={version}\nversionCode={code}\n'.encode(),
                'disable': b'', 'skip_mount': b'', 'service.sh': b'exit 99\n'}
        data.update(overrides)
        data['SHA256SUMS'] = ''.join(f'{hashlib.sha256(b).hexdigest()}  {n}\n'
                                   for n, b in sorted(data.items()) if n != 'disable').encode()
        with zipfile.ZipFile(path, 'w') as z:
            for name, blob in data.items():
                z.writestr(name, blob)
        path.with_suffix('.zip.sha256').write_text(f'{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n')
        return path

    def test_promote_is_idempotent_and_monotonic(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = self.make_zip(root)
            first = promote(archive, 'haptic-main/v1.0.0', root)
            self.assertEqual(first, promote(archive, 'haptic-main/v1.0.0', root))
            archive = self.make_zip(root, '1.0.1', 2)
            promote(archive, 'haptic-main/v1.0.1', root)
            with self.assertRaises(ValueError):
                promote(self.make_zip(root), 'haptic-main/v1.0.0', root)

    def test_reject_unsafe_or_enabled_package(self):
        for extra in ({'disable': b'not empty'}, {'../escape': b'x'}, {'system.prop': b'x'}):
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                with self.assertRaises(ValueError):
                    promote(self.make_zip(root, **extra), 'haptic-main/v1.0.0', root)

    def test_reject_bad_outer_hash_or_wrong_tag(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = self.make_zip(root)
            with self.assertRaises(ValueError):
                promote(archive, 'framebuffer10/v1.0.0', root)
            archive.with_suffix('.zip.sha256').write_text('bad\n')
            with self.assertRaises(ValueError):
                promote(archive, 'haptic-main/v1.0.0', root)


if __name__ == '__main__':
    unittest.main()
