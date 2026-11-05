#!/usr/bin/env python3
"""Regress the observed macOS build/bundle mismatch without loading native code."""
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from verify_mwnet_macos_openssl import header_version, require_version, verify


class OpenSSLIdentityTests(unittest.TestCase):
    def test_version_prefix_must_be_exact(self):
        require_version('3.6.4', 'OpenSSL 3.6.4 10 Oct 2026')
        for value in ['OpenSSL 3.0.16 11 Feb 2025', 'OpenSSL 3.6.40 1 Jan 2026', 'LibreSSL 3.6.4']:
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, 'mismatch'):
                require_version('3.6.4', value)

    def test_explicit_header_version_is_required(self):
        self.assertEqual(header_version('# define OPENSSL_VERSION_STR "3.6.4"\n'), '3.6.4')
        with self.assertRaises(ValueError):
            header_version('/* no version */')

    def test_bundled_old_library_cannot_pass_provider_check(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'include/openssl').mkdir(parents=True)
            (root / 'include/openssl/opensslv.h').write_text('#define OPENSSL_VERSION_STR "3.6.4"\n')
            (root / 'lib').mkdir()
            (root / 'lib/libcrypto.3.dylib').write_bytes(b'provider')
            bundle = root / 'package'
            bundle.mkdir()
            (bundle / 'libcrypto.3.dylib').write_bytes(b'old library')
            cache = root / 'CMakeCache.txt'
            cache.write_text(f'OPENSSL_CRYPTO_LIBRARY:FILEPATH={root}/lib/libcrypto.3.dylib\nOPENSSL_INCLUDE_DIR:PATH={root}/include\n')
            with patch('verify_mwnet_macos_openssl.library_version', side_effect=['OpenSSL 3.6.4 date', 'OpenSSL 3.0.16 date']):
                with self.assertRaisesRegex(ValueError, 'mismatch'):
                    verify(root, cache, bundle)
            cache.write_text('OPENSSL_CRYPTO_LIBRARY:FILEPATH=/another/provider\n')
            with self.assertRaisesRegex(ValueError, 'another OpenSSL'):
                verify(root, cache)


if __name__ == '__main__':
    unittest.main()
