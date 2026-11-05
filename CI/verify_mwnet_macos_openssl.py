#!/usr/bin/env python3
"""Bind macOS OpenSSL headers, CMake provider and delivered library identity."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def header_version(text):
    match = re.search(r'^\s*#\s*define\s+OPENSSL_VERSION_STR\s+"([^"]+)"', text, re.M)
    if not match:
        raise ValueError('Missing explicit OpenSSL header version')
    return match.group(1)


def library_version(path):
    # A fresh loader process prevents an already-loaded provider with the same
    # install name from masking an older library in the package under review.
    code = ('import ctypes,sys; library=ctypes.CDLL(sys.argv[1]); '
            'library.OpenSSL_version.argtypes=[ctypes.c_int]; '
            'library.OpenSSL_version.restype=ctypes.c_char_p; '
            'value=library.OpenSSL_version(0); assert value; '
            'print(value.decode("ascii"))')
    result = subprocess.run([sys.executable, '-I', '-c', code, str(path)],
                            check=True, capture_output=True, text=True, timeout=30)
    value = result.stdout.strip()
    if len(value) > 300 or '\n' in value:
        raise ValueError('Invalid runtime OpenSSL version response')
    return value


def require_version(expected, observed):
    if not observed.startswith('OpenSSL ' + expected + ' '):
        raise ValueError('OpenSSL header/runtime mismatch: ' + expected + ' versus ' + observed)


def verify(prefix, cache, package=None):
    prefix = prefix.resolve()
    header = prefix / 'include/openssl/opensslv.h'
    provider = (prefix / 'lib/libcrypto.3.dylib').resolve(strict=True)
    values = dict(line.split('=', 1) for line in cache.read_text().splitlines()
                  if '=' in line and not line.startswith(('#', '//')))
    selected = values.get('OPENSSL_CRYPTO_LIBRARY:FILEPATH')
    include = values.get('OPENSSL_INCLUDE_DIR:PATH')
    if not selected or Path(selected).resolve() != provider:
        raise ValueError('CMake selected another OpenSSL library provider')
    if not include or Path(include).resolve() != (prefix / 'include').resolve():
        raise ValueError('CMake selected another OpenSSL header provider')
    expected = header_version(header.read_text())
    observed = library_version(provider)
    require_version(expected, observed)
    report = {'scope': 'OpenSSL provider/header/runtime identity only; not graphical or complete dependency acceptance',
              'expectedVersion': expected, 'headerSha256': sha(header),
              'provider': str(provider), 'providerSha256': sha(provider),
              'providerRuntimeVersion': observed, 'cmakeCacheSha256': sha(cache), 'bundledLibraries': []}
    if package:
        package = package.resolve()
        libraries = sorted(package.rglob('libcrypto*.dylib'))
        if not libraries:
            raise ValueError('No bundled OpenSSL library found')
        for library in libraries:
            if not library.resolve().is_relative_to(package):
                raise ValueError('Bundled OpenSSL resolves outside the package')
            version = library_version(library.resolve())
            require_version(expected, version)
            report['bundledLibraries'].append({'path': str(library.relative_to(package)),
                                               'sha256': sha(library), 'runtimeVersion': version})
    report['status'] = 'pass'
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', required=True, type=Path)
    parser.add_argument('--cmake-cache', required=True, type=Path)
    parser.add_argument('--package', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Refusing to overwrite previous OpenSSL evidence')
    report = verify(args.prefix, args.cmake_cache, args.package)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('x') as stream:
        stream.write(json.dumps(report, indent=2) + '\n')
    print('PASS: OpenSSL provider, header and selected runtime versions agree')


if __name__ == '__main__':
    main()
