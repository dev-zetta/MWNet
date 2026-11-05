#!/usr/bin/env python3
"""Regress preserved-package integrity and dependency identity rejection paths."""
import hashlib
from pathlib import Path
import tempfile
import unittest
import zipfile

from inspect_mwnet_windows_dependencies import check_value, extract_checked


class PreservedWindowsDependenciesTests(unittest.TestCase):
    def test_runtime_versions_must_match_exact_component(self):
        check_value('6.6.3', '6.6.3', False)
        check_value('OpenSSL 3.6.1 date', 'OpenSSL 3.6.1 ', True)
        for value, expected, prefix in [('6.6.30', '6.6.3', False),
                                        ('OpenSSL 3.6.10 date', 'OpenSSL 3.6.1 ', True),
                                        ('libcurl/8.18.1 x', 'libcurl/8.18.0 ', True)]:
            with self.subTest(value=value), self.assertRaises(ValueError):
                check_value(value, expected, prefix)

    def test_archive_pin_and_previous_output_are_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = root / 'original.zip'
            with zipfile.ZipFile(archive, 'w') as output:
                output.writestr('MWNet/mwnet.exe', b'client')
                output.writestr('MWNet/mwnet-server.exe', b'server')
            pin = hashlib.sha256(archive.read_bytes()).hexdigest()
            with self.assertRaisesRegex(ValueError, 'hash mismatch'):
                extract_checked(archive, '0' * 64, root / 'rejected')
            self.assertFalse((root / 'rejected').exists())
            package = extract_checked(archive, pin, root / 'package')
            self.assertEqual((package / 'mwnet-server.exe').read_bytes(), b'server')
            with self.assertRaisesRegex(ValueError, 'overwrite'):
                extract_checked(archive, pin, root / 'package')

    def test_unsafe_entries_are_rejected_before_extraction(self):
        for name in ['../outside', '/absolute', 'C:/drive', 'path:stream',
                     'MWNet\\outside', 'MWNet/../outside']:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                archive = root / 'bad.zip'
                with zipfile.ZipFile(archive, 'w') as output:
                    output.writestr('MWNet/mwnet.exe', b'client')
                    output.writestr(name, b'bad')
                pin = hashlib.sha256(archive.read_bytes()).hexdigest()
                with self.assertRaisesRegex(ValueError, 'Unsafe'):
                    extract_checked(archive, pin, root / 'package')
                self.assertFalse((root / 'package').exists())

    def test_case_aliases_and_symlinks_are_rejected(self):
        for link in [False, True]:
            with self.subTest(link=link), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                archive = root / 'bad.zip'
                with zipfile.ZipFile(archive, 'w') as output:
                    output.writestr('MWNet/mwnet.exe', b'client')
                    info = zipfile.ZipInfo('MWNet/link' if link else 'mwnet/MWNET.EXE')
                    if link:
                        info.create_system = 3
                        info.external_attr = 0o120777 << 16
                    output.writestr(info, b'target')
                pin = hashlib.sha256(archive.read_bytes()).hexdigest()
                with self.assertRaisesRegex(ValueError, 'Unsafe'):
                    extract_checked(archive, pin, root / 'package')


if __name__ == '__main__':
    unittest.main()
