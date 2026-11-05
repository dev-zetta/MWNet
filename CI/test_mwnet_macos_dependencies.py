#!/usr/bin/env python3
"""Regress native probe dispatch/origin/version checks without claiming Mac runs."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from verify_mwnet_macos_dependencies import check_response


@unittest.skipUnless(sys.platform.startswith('linux') and shutil.which('cc'), 'Linux C ABI regression requires cc')
class NativeProbeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='native-provider-regression-')
        cls.root = Path(cls.temporary.name)
        cls.helper = cls.root / 'probe'
        source = Path(__file__).with_name('mwnet_native_library_probe.c')
        subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', str(source),
                        '-o', str(cls.helper), '-ldl'], check=True, capture_output=True)
        stub = cls.root / 'stub.c'
        stub.write_text('''
#include <stddef.h>
const char *qVersion(void) { return "6.10.3"; }
const char *sodium_version_string(void) { return "1.0.22"; }
const char *png_get_libpng_ver(const void *unused) { (void)unused; return "1.6.54"; }
int FcGetVersion(void) { return 21803; }
int libintl_version = 0x010000;
int FT_Init_FreeType(void **instance) {
#ifdef FAIL_FT
    (void)instance; return 1;
#else
    *instance = (void *)1; return 0;
#endif
}
void FT_Library_Version(void *instance, int *a, int *b, int *c) { (void)instance; *a=2; *b=13; *c=3; }
int FT_Done_FreeType(void *instance) { (void)instance; return 0; }
''')
        cls.library = cls.root / 'libfixture.so'
        cls.broken = cls.root / 'libbroken.so'
        for destination, flags in [(cls.library, []), (cls.broken, ['-DFAIL_FT'])]:
            subprocess.run(['cc', '-shared', '-fPIC', *flags, str(stub), '-o', str(destination)],
                           check=True, capture_output=True)
        wrapper = cls.root / 'wrapper.c'
        wrapper.write_text('extern const char *qVersion(void); const char *forward(void) { return qVersion(); }\n')
        cls.wrapper = cls.root / 'libwrapper.so'
        subprocess.run(['cc', '-shared', '-fPIC', str(wrapper), '-L' + str(cls.root), '-lfixture',
                        '-Wl,-rpath,' + str(cls.root), '-o', str(cls.wrapper)], check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def probe(self, library, kind):
        return subprocess.run([str(self.helper), str(library), kind], capture_output=True, text=True, timeout=15)

    def test_native_c_abi_dispatch_and_cleanup(self):
        expected = {'qt': '6.10.3', 'sodium': '1.0.22', 'png': '1.6.54',
                    'fontconfig': '2.18.3', 'intl': '1.0.0', 'freetype': '2.13.3'}
        for kind, version in expected.items():
            with self.subTest(kind=kind):
                result = self.probe(self.library, kind)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(check_response(result.stdout, self.library, version, kind)['runtimeVersion'], version)

    def test_initialization_and_wrong_symbol_origin_fail(self):
        result = self.probe(self.broken, 'freetype')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('FT_Init_FreeType failed', result.stderr)
        result = self.probe(self.wrapper, 'qt')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('different library provider', result.stderr)

    def test_version_and_functional_evidence_cannot_be_substituted(self):
        result = self.probe(self.library, 'qt')
        with self.assertRaisesRegex(ValueError, 'version mismatch'):
            check_response(result.stdout, self.library, '6.10.30', 'qt')
        with self.assertRaisesRegex(ValueError, 'path mismatch'):
            check_response(result.stdout, self.broken, '6.10.3', 'qt')
        with self.assertRaisesRegex(ValueError, 'functional coverage'):
            check_response(result.stdout, self.library, '6.10.3', 'brotli')
        with self.assertRaisesRegex(ValueError, 'Invalid'):
            check_response(result.stdout + 'extra\n', self.library, '6.10.3', 'qt')

    def test_tiny_vector_with_real_local_brotli_decoder(self):
        selected = subprocess.check_output(['cc', '-print-file-name=libbrotlidec.so.1'], text=True).strip()
        if not Path(selected).is_file():
            self.skipTest('No local Brotli decoder; native job still requires actual package decode')
        library = Path(selected).resolve()
        result = self.probe(library, 'brotli')
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[2], 'version-and-tiny-vector-decode')
        self.assertEqual(Path(lines[0]), library)


if __name__ == '__main__':
    unittest.main()
