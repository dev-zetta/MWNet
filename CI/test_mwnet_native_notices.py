#!/usr/bin/env python3
"""Regress the actual Qt/FreeType notice-selection gaps and source pin guards."""
import hashlib
import json
from pathlib import Path
import tempfile
import tarfile
import io
import unittest
import zipfile

from assemble_mwnet_native_notices import archive_notices, collect_sources, legal_member


class NativeNoticeTests(unittest.TestCase):
    def test_license_directories_and_ftl_are_selected_but_code_and_certificates_are_not(self):
        for name in ['pkg/LICENSES/MIT.txt', 'pkg/docs/FTL.TXT', 'pkg/docs/GPLv2.TXT',
                     'pkg/LICENSE_1_0.txt', 'pkg/src/qt_attribution.json']:
            with self.subTest(name=name):
                self.assertTrue(legal_member(name))
        for name in ['pkg/test/ext-userNotice.pem', 'pkg/crypto/v3_usernotice.c',
                     'pkg/util/copyright.pm', 'pkg/src/tools/no-copyright']:
            with self.subTest(name=name):
                self.assertFalse(legal_member(name))

    def test_explicit_qt_license_reference_preserves_exact_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / 'source.zip'
            license_bytes = b'Exact upstream text\r\nCopyright\r\n'
            with zipfile.ZipFile(archive, 'w') as output:
                output.writestr('pkg/thirdparty/qt_attribution.json', json.dumps({'Name': 'Fixture', 'LicenseFile': '../legal/body.dat'}))
                output.writestr('pkg/legal/body.dat', license_bytes)
                output.writestr('pkg/docs/FTL.TXT', b'FreeType notice')
                output.writestr('pkg/test/ext-userNotice.pem', b'not a notice')
            notices, references, missing, diagnostics = archive_notices(archive, 'pinned:source')
            self.assertFalse(diagnostics)
            self.assertFalse(missing)
            self.assertEqual(len(references), 1)
            self.assertEqual(references[0]['sha256'], hashlib.sha256(license_bytes).hexdigest())
            self.assertIn(license_bytes, [row['bytes'] for row in notices])
            self.assertFalse(any('ext-userNotice' in row['origin'] for row in notices))

    def test_missing_and_cross_root_qt_references_remain_open(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / 'source.zip'
            with zipfile.ZipFile(archive, 'w') as output:
                output.writestr('pkg/src/qt_attribution.json', json.dumps([
                    {'Name': 'Missing', 'LicenseFile': 'absent'},
                    {'Name': 'Outside', 'LicenseFile': '../../other/LICENSE'}]))
                output.writestr('other/LICENSE', b'not within this project root')
            _, checked, missing, _ = archive_notices(archive, 'pinned:source')
            self.assertFalse(checked)
            self.assertEqual(len(missing), 2)

    def test_upstream_literal_newlines_are_preserved_and_recorded(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / 'source.tar.gz'
            original = b'{"Name":"Fixture", "Copyright":"line one\nline two", "LicenseFile":"../LICENSES/MIT.txt"}'
            with tarfile.open(archive, 'w:gz') as output:
                for name, data in [('pkg/src/qt_attribution.json', original),
                                   ('pkg/LICENSES/MIT.txt', b'Exact license\r\n')]:
                    info = tarfile.TarInfo(name)
                    info.size = len(data)
                    output.addfile(info, io.BytesIO(data))
            notices, checked, missing, diagnostics = archive_notices(archive, 'pinned:source')
            self.assertFalse(missing)
            self.assertEqual(len(checked), 1)
            self.assertEqual(len(diagnostics), 1)
            self.assertEqual(diagnostics[0]['sha256'], hashlib.sha256(original).hexdigest())
            self.assertIn(original, [row['bytes'] for row in notices])

    def test_tampered_or_partial_sources_cannot_supply_notice_material(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            data = b'original'
            (root / 'source.zip').write_bytes(data)
            digest = hashlib.sha256(data).hexdigest()
            receipt = {'status': 'complete-for-listed-inputs', 'sources': [
                {'filename': 'source.zip', 'algorithm': 'sha256', 'checksum': digest, 'sha256': digest}]}
            path = root / 'source-delivery.json'
            path.write_text(json.dumps(receipt))
            self.assertEqual(len(collect_sources([root])), 1)
            (root / 'source.zip').write_bytes(b'tampered')
            with self.assertRaisesRegex(ValueError, 'pin changed'):
                collect_sources([root])
            receipt['status'] = 'running'
            path.write_text(json.dumps(receipt))
            with self.assertRaisesRegex(ValueError, 'not complete'):
                collect_sources([root])

    def test_other_malformed_qt_metadata_is_not_accepted(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / 'source.zip'
            with zipfile.ZipFile(archive, 'w') as output:
                output.writestr('pkg/qt_attribution.json', '{"Name":"Fixture",}')
            with self.assertRaisesRegex(ValueError, 'Invalid Qt attribution metadata'):
                archive_notices(archive, 'pinned:source')


if __name__ == '__main__':
    unittest.main()
