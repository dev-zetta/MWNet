#!/usr/bin/env python3
"""Private content bundle integrity and exclusion checks, without proprietary assets."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zlib

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('content_bundle', ROOT / 'docker/discovery/gameplay/prepare.py')
BUNDLE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUNDLE)


class ContentBundleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.data = self.root / 'data'
        self.data.mkdir()
        defaults = self.root / 'files/mwnet/core-scripts/defaults'
        defaults.mkdir(parents=True)
        allowed = []
        for name in BUNDLE.NAMES:
            contents = b'TES3' + name.encode()
            (self.data / name).write_bytes(contents)
            allowed.append({name: [hex(zlib.crc32(contents))]})
        (defaults / 'requiredDataFiles.json').write_text(json.dumps(allowed))
        (self.data / 'private-account.json').write_text('must not be copied')
        self.output = self.root / 'bundle'
        self.receipt = {'outcome': 'pass', 'gameplayProtocol': 13, 'fingerprint': 'public-test',
                        'imageConfigDigest': 'accepted-test-image', 'origin': 'https://example.test',
                        'gameEndpoint': {'host': '209.25.141.17', 'port': 43133}}
        context = patch.object(BUNDLE, 'ROOT', self.root)
        context.start()
        self.addCleanup(context.stop)

    def test_ordered_content_only_and_private_bundle(self):
        result = BUNDLE.prepare(self.data, self.output, self.receipt)
        self.assertEqual(tuple(result['files']), BUNDLE.NAMES)
        self.assertEqual({p.name for p in (self.output / 'content').iterdir()}, set(BUNDLE.NAMES))
        self.assertEqual(self.output.stat().st_mode & 0o777, 0o700)
        self.assertNotIn('private-account', (self.output / 'SHA256SUMS').read_text())

    def test_altered_content_rejected_before_creating_bundle(self):
        (self.data / 'Morrowind.esm').write_bytes(b'TES3changed')
        with self.assertRaisesRegex(ValueError, 'CRC32'):
            BUNDLE.prepare(self.data, self.output, self.receipt)
        self.assertFalse(self.output.exists())

    def test_missing_content_rejected(self):
        (self.data / 'Bloodmoon.esm').unlink()
        with self.assertRaisesRegex(ValueError, 'Missing'):
            BUNDLE.prepare(self.data, self.output, self.receipt)

    def test_failed_registration_receipt_rejected(self):
        self.receipt['outcome'] = 'failed'
        with self.assertRaisesRegex(ValueError, 'successful'):
            BUNDLE.prepare(self.data, self.output, self.receipt)
        self.assertFalse(self.output.exists())


if __name__ == '__main__':
    unittest.main()
