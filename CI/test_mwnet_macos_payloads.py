#!/usr/bin/env python3
"""Reject malformed payload evidence and distinguish code from loader changes."""
import struct
import unittest

from inspect_mwnet_macos_payloads import macho_payloads, sdk_member_paths


def thin(text=b'code', loader=b'original', cpu=0x1000007):
    # One section and a padded LC_ID_DYLIB; loader edits leave section bytes fixed.
    header = struct.pack('<8I', 0xfeedfacf, cpu, 3, 6, 2, 184, 0, 0)
    segment = struct.pack('<II16s4Q4I', 0x19, 152, b'__TEXT', 0, 256,
                          0, 256, 7, 5, 1, 0)
    section = struct.pack('<16s16sQQ8I', b'__text', b'__TEXT', 256, len(text),
                          256, 2, 0, 0, 0x80000400, 0, 0, 0)
    loader_command = struct.pack('<6I', 0xd, 32, 24, 0, 0x10000, 0x10000) + loader[:7].ljust(8, b'\0')
    return (header + segment + section + loader_command).ljust(256, b'\0') + text


class MachOPayloadTests(unittest.TestCase):
    def test_sdk_alias_chains_are_not_extracted_as_provider_files(self):
        prefix = 'sdk/installed/arm64-osx-dynamic/lib/'
        rows = [('libA.1.2.dylib', 'A -rwxr-xr-x'),
                ('libA.1.dylib', 'A lrwxr-xr-x'),
                ('libA.dylib', 'A lrwxr-xr-x'),
                ('plugins/plugin.so', 'A -rwxr-xr-x')]
        listing = '\n\n'.join(f'Path = {prefix}{name}\nAttributes = {attributes}' for name, attributes in rows)
        self.assertEqual(sdk_member_paths(listing), [prefix + rows[i][0] for i in [0, 3]])
        with self.assertRaisesRegex(ValueError, 'Unsafe'):
            sdk_member_paths(f'Path = {prefix}../escape.dylib\nAttributes = A -rwxr-xr-x')

    def test_loader_changes_are_separate_from_code_identity(self):
        original, changed = [macho_payloads(x)[0] for x in [thin(), thin(loader=b'changed')]]
        self.assertEqual(original['sectionPayloadSha256'], changed['sectionPayloadSha256'])
        self.assertNotEqual(original['installName'], changed['installName'])
        self.assertNotEqual(original['sectionPayloadSha256'], macho_payloads(thin(text=b'evil'))[0]['sectionPayloadSha256'])

    def test_truncated_or_out_of_bounds_sections_cannot_match(self):
        with self.assertRaises(ValueError):
            macho_payloads(thin()[:-1])
        for offset, value in [(16, 4097), (36, 0), (104 + 40, 10000), (104 + 48, 1)]:
            data = bytearray(thin())
            struct.pack_into('<I', data, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                macho_payloads(bytes(data))

    def test_zero_fill_is_metadata_not_file_content(self):
        data = bytearray(thin())
        struct.pack_into('<I', data, 104 + 48, 0)  # file offset
        struct.pack_into('<I', data, 104 + 64, 1)  # S_ZEROFILL
        payload = macho_payloads(bytes(data))[0]
        self.assertTrue(payload['sections'][0]['zeroFill'])
        self.assertIsNone(payload['sections'][0]['sha256'])
        self.assertFalse(payload['sectionPayloadComparable'])
        self.assertIsNone(payload['sectionPayloadSha256'])

    def test_empty_compatibility_library_cannot_be_a_provider_match(self):
        payload = macho_payloads(thin(text=b''))[0]
        self.assertFalse(payload['sectionPayloadComparable'])
        self.assertIsNone(payload['sectionPayloadSha256'])
        self.assertIsNotNone(payload['installName'])

    def test_relocation_changes_are_part_of_section_identity(self):
        original = bytearray(thin() + b'reloc123')
        struct.pack_into('<2I', original, 104 + 56, 260, 1)
        changed = original.copy()
        changed[-1] ^= 1
        self.assertNotEqual(macho_payloads(bytes(original))[0]['sectionPayloadSha256'],
                            macho_payloads(bytes(changed))[0]['sectionPayloadSha256'])

    def test_universal_slices_are_architecture_bound(self):
        first, second = thin(), thin(cpu=0x100000c)
        header = struct.pack('>II', 0xcafebabe, 2)
        table = struct.pack('>5I', 0x1000007, 3, 4096, len(first), 12)
        table += struct.pack('>5I', 0x100000c, 3, 8192, len(second), 12)
        data = (header + table).ljust(4096, b'\0') + first
        data = data.ljust(8192, b'\0') + second
        self.assertEqual(len(macho_payloads(data)), 2)
        mismatch = bytearray(data)
        struct.pack_into('>I', mismatch, 8, 0x100000c)
        with self.assertRaisesRegex(ValueError, 'mismatch'):
            macho_payloads(bytes(mismatch))
        overlap = bytearray(data)
        struct.pack_into('>I', overlap, 28 + 8, 4096)
        with self.assertRaisesRegex(ValueError, 'Overlapping'):
            macho_payloads(bytes(overlap))


if __name__ == '__main__':
    unittest.main()
