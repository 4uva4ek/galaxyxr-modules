"""Version selection and private-input containment; no GitHub/device writes."""
import base64
import hashlib
import json
import lzma
import os
from pathlib import Path
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

import ci_modules as ci


def props(version='1.2.3', code=10):
    return {'version':version,'versionCode':str(code)}


class Selection(unittest.TestCase):
    def test_both_fields_must_increase(self):
        self.assertTrue(ci.bumped(props(),props('1.2.4',11)))
        self.assertFalse(ci.bumped(props(),props()))
        for value in (props('1.2.4',10),props('1.2.3',11),props('1.2.2',12),props('1.2.4',9),props('dev',11)):
            with self.subTest(value=value), self.assertRaises(ValueError): ci.bumped(props(),value)

    def test_unchanged_and_unpublished_filter(self):
        with patch.object(ci,'MODULES',{'one':'gxr_one','two':'gxr_two'}), \
             patch.object(ci,'pending',side_effect=lambda name:name=='one'):
            self.assertEqual(ci.plan(publish=True),['one'])
            self.assertEqual(ci.plan(),['one','two'])
            with self.assertRaises(ValueError):ci.plan(selection='framebuffer10')

    def test_every_push_compares_to_published_feed(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(ci,'ROOT',Path(directory)), \
             patch.object(ci,'MODULES',{'one':'gxr_one'}):
            root=Path(directory);(root/'updates').mkdir()
            (root/'updates/one.json').write_text(json.dumps(props()))
            with patch.object(ci,'source_props',return_value={**props(),'author':'changed'}):
                self.assertEqual(ci.plan(publish=True),[])
            # A version raised in an earlier commit remains pending on any later push.
            with patch.object(ci,'source_props',return_value=props('1.2.4',11)):
                self.assertEqual(ci.plan(publish=True),['one'])
                (root/'updates/one.json').write_text(json.dumps(props('1.2.4',11)))
                self.assertEqual(ci.plan(publish=True),[])

    def test_first_source_and_codes(self):
        self.assertTrue(ci.bumped(None,props('0.1.0',1)))
        for code in ('0','-1','2147483648','x'):
            with self.subTest(code=code),self.assertRaises(ValueError):ci.bumped(None,props('0.1.0',code))


class PrivateInputs(unittest.TestCase):
    def test_integrity_and_missing_input(self):
        data=b'fixture input, never a real vendor binary'
        encoded=base64.b64encode(lzma.compress(data)).decode()
        with tempfile.TemporaryDirectory() as directory, \
             patch.dict(ci.INPUTS, {'fixture':('FIXTURE_',1,'fixture.bin',hashlib.sha256(data).hexdigest())}):
            path=Path(directory)
            with patch.dict(os.environ,{'FIXTURE_01':encoded}):
                ci.restore_input('fixture',path)
                self.assertEqual((path/'fixture.bin').read_bytes(),data)
            with patch.dict(os.environ,{'FIXTURE_01':''}),self.assertRaises(ValueError):
                ci.restore_input('fixture',path)
            bad=base64.b64encode(lzma.compress(b'changed')).decode()
            with patch.dict(os.environ,{'FIXTURE_01':bad}),self.assertRaises(ValueError):
                ci.restore_input('fixture',path)
            self.assertEqual((path/'fixture.bin').read_bytes(),data)


class ReleaseLookup(unittest.TestCase):
    def test_draft_on_later_page(self):
        draft = {'tag_name': 'one/v1.0.0', 'draft': True, 'assets': []}
        replies = [SimpleNamespace(returncode=1, stderr='HTTP 404'),
                   SimpleNamespace(returncode=0, stdout=json.dumps([[], [draft]]))]
        with patch.object(ci.subprocess, 'run', side_effect=replies):
            self.assertEqual(ci.release_info('one/v1.0.0'), draft)

    def test_absent_and_failed_draft_lookup(self):
        for response, fails in ((SimpleNamespace(returncode=0, stdout='[[]]'), False),
                                (SimpleNamespace(returncode=1), True)):
            with self.subTest(fails=fails), patch.object(ci.subprocess, 'run', side_effect=[
                    SimpleNamespace(returncode=1, stderr='HTTP 404'), response]):
                if fails:
                    with self.assertRaises(RuntimeError): ci.release_info('one/v1.0.0')
                else:
                    self.assertIsNone(ci.release_info('one/v1.0.0'))


class Publication(unittest.TestCase):
    def exercise(self, existing=None, tag_commit=None, verify_error=False, lookup=None):
        calls=[]
        commit='a'*40
        item={'version':'1.2.4','versionCode':11,'tag':'one/v1.2.4',
              'archive':'dist/one/gxr-one-1.2.4-auto.zip','sha256':hashlib.sha256(b'zip').hexdigest()}
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            archive=root/item['archive'];archive.parent.mkdir(parents=True)
            archive.write_bytes(b'zip');archive.with_suffix('.zip.sha256').write_text('fixture\n')
            (root/'changelogs').mkdir();(root/'changelogs/one.md').write_text('## 1.2.4\nValidated fix\n')
            manifest=root/'dist/ci-manifest.json'
            manifest.write_text(json.dumps({'commit':commit,'modules':{'one':item}}))

            def fake_run(*args, **kwargs):
                calls.append(args)
                if args[:4]==('git','diff','--cached','--name-only'): return 'updates/one.json'
                return SimpleNamespace(returncode=0)

            result=SimpleNamespace(returncode=0 if tag_commit else 1,stdout=(tag_commit or '')+'\n')
            responses=lookup if lookup is not None else ([existing] if existing else [None,{'draft':True,'assets':[]}])
            with patch.object(ci,'ROOT',root),patch.object(ci,'MODULES',{'one':'gxr_one'}), \
                 patch.dict(os.environ,{'GITHUB_SHA':commit}),patch.object(ci,'run',side_effect=fake_run), \
                 patch.object(ci.subprocess,'run',return_value=result), \
                 patch.object(ci,'release_info',side_effect=responses),patch.object(ci,'validate',return_value=11), \
                 patch.object(ci,'verify_download',side_effect=ValueError('mismatching assets') if verify_error else None), \
                 patch.object(ci,'promote') as promote:
                error=None
                try: ci.publish(manifest)
                except (ValueError,RuntimeError) as e:error=str(e)
                return calls,promote.called,error

    def test_new_draft_verified_before_publication_and_feed(self):
        calls,promoted,error=self.exercise()
        self.assertIsNone(error)
        self.assertTrue(promoted)
        self.assertTrue(any(c[:3]==('gh','release','create') and '--draft' in c for c in calls))
        self.assertEqual(sum(c[:3]==('gh','release','upload') for c in calls),2)
        self.assertTrue(any(c[:3]==('gh','release','edit') and '--draft=false' in c for c in calls))
        self.assertFalse(any('--clobber' in c or '--force' in c for c in calls))

    def test_existing_release_is_verified_without_upload(self):
        calls,promoted,error=self.exercise({'draft':False,'prerelease':False},'a'*40)
        self.assertIsNone(error);self.assertTrue(promoted)
        self.assertFalse(any(c[:2]==('gh','release') for c in calls))

    def test_conflicting_tag_or_assets_never_promote(self):
        for kwargs in ({'tag_commit':'b'*40}, {'verify_error':True}):
            with self.subTest(kwargs=kwargs):
                calls,promoted,error=self.exercise(**kwargs)
                self.assertIsNotNone(error);self.assertFalse(promoted)
                self.assertFalse(any(c[:3]==('gh','release','edit') for c in calls))

    def test_created_release_becomes_visible_after_retry(self):
        with patch.object(ci.time,'sleep') as sleep:
            calls,promoted,error=self.exercise(lookup=[None,None,{'draft':True,'assets':[]}])
        self.assertIsNone(error);self.assertTrue(promoted)
        self.assertEqual(sleep.call_count,1)
        self.assertTrue(any(c[:3]==('gh','release','edit') for c in calls))

    def test_created_release_never_visible_stops_after_bounded_retries(self):
        with patch.object(ci.time,'sleep') as sleep:
            calls,promoted,error=self.exercise(lookup=[None]+[None]*ci.RELEASE_LOOKUP_ATTEMPTS)
        self.assertEqual(error,'Created release is not visible; retry publication later')
        self.assertFalse(promoted)
        self.assertEqual(sleep.call_count,ci.RELEASE_LOOKUP_ATTEMPTS-1)
        self.assertFalse(any(c[:3]==('gh','release','edit') for c in calls))


if __name__=='__main__':unittest.main(verbosity=2)
