#!/usr/bin/env python3
"""Protect original app bytes/modes/links while changing enclosing signatures."""
from pathlib import Path
import struct
import tempfile
import unittest

from prepare_mwnet_macos_notice_candidate import RESOURCE_NAMES, check_changes, inventory, signature_invariant


def signed_binary(text=b'code', loader=b'libA', signature=b'A' * 16):
    header = struct.pack('<8I', 0xfeedfacf, 0x1000007, 3, 2, 4, 272, 0, 0)
    segment = struct.pack('<II16s4Q4I', 0x19, 152, b'__TEXT', 0, 4096, 0, 400, 7, 5, 1, 0)
    section = struct.pack('<16s16sQQ8I', b'__text', b'__TEXT', 384, len(text), 384, 2, 0, 0, 0x80000400, 0, 0, 0)
    linkedit = struct.pack('<II16s4Q4I', 0x19, 72, b'__LINKEDIT', 4096, 4096, 400, len(signature), 7, 1, 0, 0)
    dylib = struct.pack('<6I', 0xc, 32, 24, 0, 0x10000, 0x10000) + loader[:7].ljust(8, b'\0')
    signing = struct.pack('<4I', 0x1d, 16, 400, len(signature))
    return (header + segment + section + linkedit + dylib + signing).ljust(384, b'\0') + text + b'\0' * (16 - len(text)) + signature


class MacNoticeCandidateTests(unittest.TestCase):
    def test_only_signing_blob_and_its_sizes_can_change(self):
        self.assertEqual(signature_invariant(signed_binary()), signature_invariant(signed_binary(signature=b'B' * 80)))

    def test_code_and_loader_changes_are_rejected(self):
        original = signature_invariant(signed_binary())
        self.assertNotEqual(original, signature_invariant(signed_binary(text=b'evil')))
        self.assertNotEqual(original, signature_invariant(signed_binary(loader=b'libB')))

    def test_unsigned_padding_and_symbol_bytes_are_also_protected(self):
        original = signed_binary()
        changed = bytearray(original)
        changed[380] = 1
        self.assertNotEqual(signature_invariant(original), signature_invariant(changed))

    def test_missing_or_invalid_signing_region_is_rejected(self):
        with self.assertRaises(ValueError):
            signature_invariant(signed_binary()[:-1])
        changed = bytearray(signed_binary())
        struct.pack_into('<I', changed, 288 + 8, 0)
        with self.assertRaises(ValueError):
            signature_invariant(changed)

    def create_app(self, root):
        for name, data in [('Contents/MacOS/main', signed_binary()), ('Contents/Frameworks/libA.dylib', b'original library'),
                           ('Contents/Resources/scripts.lua', b'original script'), ('Contents/_CodeSignature/CodeResources', b'old seal')]:
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        (root / 'Contents/MacOS/main').chmod(0o755)
        (root / 'Contents/Resources/Alias').symlink_to('scripts.lua')

    def add_resources(self, root):
        resources = root / 'Contents/Resources/distribution'
        resources.mkdir()
        for name in RESOURCE_NAMES:
            (resources / name).write_text('verbatim notice/reference')

    def test_notice_and_signature_changes_are_explicitly_bounded(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.create_app(root)
            before = inventory(root)
            self.add_resources(root)
            (root / 'Contents/_CodeSignature/CodeResources').write_bytes(b'new seal')
            (root / 'Contents/MacOS/main').write_bytes(signed_binary(signature=b'B' * 32))
            result = check_changes(before, inventory(root), 'main')
            self.assertEqual(len(result['changedSigningPaths']), 2)
            self.assertEqual(len(result['addedPaths']), 5)
            self.assertEqual(result['removedSigningPaths'], [])

    def test_library_script_or_mode_mutation_cannot_pass(self):
        for change in ['library', 'script', 'mode']:
            with self.subTest(change=change), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                self.create_app(root)
                before = inventory(root)
                self.add_resources(root)
                if change == 'library':
                    (root / 'Contents/Frameworks/libA.dylib').write_bytes(b'changed')
                elif change == 'script':
                    (root / 'Contents/Resources/scripts.lua').unlink()
                else:
                    (root / 'Contents/MacOS/main').chmod(0o644)
                with self.assertRaises(ValueError):
                    check_changes(before, inventory(root), 'main')

    def test_existing_distribution_files_are_not_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.create_app(root)
            self.add_resources(root)
            with self.assertRaisesRegex(ValueError, 'overwrite'):
                check_changes(inventory(root), inventory(root), 'main')

    def test_app_link_may_not_escape_to_host_state(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'escape').symlink_to('../outside')
            with self.assertRaisesRegex(ValueError, 'leaves'):
                inventory(root)


if __name__ == '__main__':
    unittest.main()
