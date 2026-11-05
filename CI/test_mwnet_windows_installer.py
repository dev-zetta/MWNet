#!/usr/bin/env python3
"""Regress payload drift and destination/preservation checks without installing."""
from pathlib import Path
import tempfile
import unittest

from verify_mwnet_windows_installer import compare_installed, validate_destination


class InstallerEvidenceTests(unittest.TestCase):
    def test_only_documented_redist_removal_and_uninstaller_addition_are_allowed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            portable, installed = root / 'portable', root / 'installed'
            (portable / 'redist').mkdir(parents=True)
            installed.mkdir()
            (portable / 'redist/vc_redist.x64.exe').write_bytes(b'redist')
            for name in ['mwnet.exe', 'mwnet-server.exe']:
                (portable / name).write_bytes(b'original')
                (installed / name).write_bytes(b'original')
            (installed / 'Uninstall.exe').write_bytes(b'generated')
            self.assertEqual(compare_installed(portable, installed)['byteIdenticalInstalledPayloadFiles'], 2)
            (installed / 'mwnet-server.exe').write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError, 'changed'):
                compare_installed(portable, installed)

    def test_missing_payloads_and_unexpected_extras_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            portable, installed = root / 'portable', root / 'installed'
            (portable / 'redist').mkdir(parents=True)
            installed.mkdir()
            (portable / 'redist/vc_redist.x64.exe').write_bytes(b'redist')
            (portable / 'mwnet.exe').write_bytes(b'client')
            (installed / 'Uninstall.exe').write_bytes(b'generated')
            with self.assertRaisesRegex(ValueError, 'missing'):
                compare_installed(portable, installed)
            (installed / 'mwnet.exe').write_bytes(b'client')
            (installed / 'unexpected.txt').write_bytes(b'extra')
            with self.assertRaisesRegex(ValueError, 'unexpected'):
                compare_installed(portable, installed)

    def test_existing_and_outside_destinations_are_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            workspace = root / 'workspace'
            workspace.mkdir()
            target = validate_destination(workspace, workspace / 'evidence')
            self.assertEqual(target, workspace / 'evidence')
            target.mkdir()
            (target / 'keep').write_bytes(b'previous')
            with self.assertRaisesRegex(ValueError, 'overwrite'):
                validate_destination(workspace, target)
            self.assertEqual((target / 'keep').read_bytes(), b'previous')
            for candidate in [root / 'outside', workspace]:
                with self.subTest(candidate=candidate), self.assertRaisesRegex(ValueError, 'inside'):
                    validate_destination(workspace, candidate)

    def test_nsis_destination_requires_no_quotes_or_spaces(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ['space here', 'quote"here']:
                with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'whitespace-free'):
                    validate_destination(root, root / name)


if __name__ == '__main__':
    unittest.main()
