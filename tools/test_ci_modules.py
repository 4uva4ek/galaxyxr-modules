"""Version selection and private-input containment; no GitHub/device writes."""
import base64
import hashlib
import json
import lzma
import os
from pathlib import Path
import tempfile
import unittest
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
             patch.object(ci,'source_props',return_value=props('1.2.4',11)), \
             patch.object(ci,'run',return_value='version=1.2.3\nversionCode=10'), \
             patch.object(ci,'pending',side_effect=lambda name:name=='one'):
            self.assertEqual(ci.plan('a'*40),['one'])
            self.assertEqual(ci.plan(publish=True),['one'])
            self.assertEqual(ci.plan(),['one','two'])
            with self.assertRaises(ValueError):ci.plan(selection='framebuffer10')
            with self.assertRaises(ValueError):ci.plan('0'*40)

    def test_metadata_only_does_not_release(self):
        with patch.object(ci,'MODULES',{'one':'gxr_one'}), \
             patch.object(ci,'source_props',return_value=props()), \
             patch.object(ci,'run',return_value='version=1.2.3\nversionCode=10\nauthor=old'):
            self.assertEqual(ci.plan('a'*40),[])

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


if __name__=='__main__':unittest.main(verbosity=2)
