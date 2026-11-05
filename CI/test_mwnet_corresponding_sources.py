#!/usr/bin/env python3
"""Source-delivery regressions; no network, game assets or binary execution."""
import hashlib
from pathlib import Path
import tempfile
import unittest

from collect_mwnet_corresponding_sources import collect, validate


class SourceDeliveryTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.row = {'filename': 'source.tar.gz', 'algorithm': 'sha256',
                    'checksum': hashlib.sha256(b'source').hexdigest(), 'urls': ['https://example.org/source.tar.gz']}

    def test_unpinned_unsafe_or_duplicate_input_is_refused(self):
        for change in [{'checksum': ''}, {'algorithm': 'md5'}, {'filename': '../escape'},
                       {'urls': ['http://example.org/source']}, {'urls': ['https://user:secret@example.org/source']}]:
            with self.subTest(change=change), self.assertRaises(ValueError):
                validate({'schemaVersion': 1, 'sources': [{**self.row, **change}]})
        with self.assertRaises(ValueError):
            validate({'schemaVersion': 1, 'sources': [self.row, self.row]})

    def test_delivery_verifies_downloaded_bytes(self):
        output = self.root / 'out'
        report = collect({'schemaVersion': 1, 'sources': [self.row]}, self.root / 'cache', output,
                         lambda row, path: path.write_bytes(b'source'))
        self.assertEqual(report['status'], 'complete-for-listed-inputs')
        self.assertEqual((output / self.row['filename']).read_bytes(), b'source')
        self.assertEqual(report['sources'][0]['sha256'], self.row['checksum'])

    def test_bad_download_cannot_be_accepted(self):
        output = self.root / 'out'
        with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
            collect({'schemaVersion': 1, 'sources': [self.row]}, self.root / 'cache', output,
                    lambda row, path: path.write_bytes(b'changed'))
        self.assertIn('"status": "failed"', (output / 'source-delivery.json').read_text())

    def test_cached_bytes_are_rechecked(self):
        cache = self.root / 'cache'
        cache.mkdir()
        (cache / self.row['filename']).write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
            collect({'schemaVersion': 1, 'sources': [self.row]}, cache, self.root / 'out')

    def test_previous_receipt_is_not_overwritten(self):
        output = self.root / 'out'
        output.mkdir()
        (output / 'source-delivery.json').write_text('original')
        with self.assertRaisesRegex(ValueError, 'Output must be new'):
            collect({'schemaVersion': 1, 'sources': [self.row]}, self.root / 'cache', output)
        self.assertEqual((output / 'source-delivery.json').read_text(), 'original')


if __name__ == '__main__':
    unittest.main()
