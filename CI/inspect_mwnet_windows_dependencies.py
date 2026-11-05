#!/usr/bin/env python3
"""Read versions from the preserved Windows package, without rebuilding it.

File metadata and native library APIs provide scoped identity evidence. They do
not prove corresponding-source completeness, installation or graphical gameplay.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import sys
import zipfile

from mwnet_temp import configure_temp_environment


PROBES = [
    ('Qt6Core.dll', 'qVersion', [], '6.6.3', False),
    ('libcrypto-3-x64.dll', 'OpenSSL_version', [0], 'OpenSSL 3.6.1 ', True),
    ('libsodium.dll', 'sodium_version_string', [], '1.0.21', False),
    ('libcurl.dll', 'curl_version', [], 'libcurl/8.18.0 ', True),
    ('sqlite3.dll', 'sqlite3_libversion', [], '3.51.2', False),
    ('zlib1.dll', 'zlibVersion', [], '1.3.1', False),
]


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def extract_checked(archive, expected, output):
    if not re.fullmatch('[0-9a-f]{64}', expected) or sha(archive) != expected:
        raise ValueError('Preserved Windows archive hash mismatch')
    if output.exists():
        raise ValueError('Refusing to overwrite an extracted package')
    with zipfile.ZipFile(archive) as source:
        names = set()
        for row in source.infolist():
            path = PurePosixPath(row.filename)
            mode = row.external_attr >> 16
            # Reject Windows drive/stream names and case aliases as well as zip
            # traversal and links. Inspect the complete list before any writes.
            if (not row.filename or path.is_absolute() or '..' in path.parts
                    or '\\' in row.filename or ':' in row.filename
                    or stat.S_ISLNK(mode) or row.filename.casefold() in names):
                raise ValueError('Unsafe or duplicate package entry')
            names.add(row.filename.casefold())
        if source.testzip() is not None:
            raise ValueError('Preserved package failed ZIP CRC validation')
        output.mkdir(parents=True)
        source.extractall(output)
    clients = list(output.rglob('mwnet.exe'))
    if len(clients) != 1 or not (clients[0].parent / 'mwnet-server.exe').is_file():
        raise ValueError('Expected exactly one complete portable client/server package')
    return clients[0].parent.resolve(strict=True)


def native_probe(path, symbol, arguments):
    # Each library gets a fresh process. Ignore PATH providers; permit the
    # package directory and Windows system libraries, and confirm the handle.
    code = r'''
import ctypes,json,os,pathlib,sys
p=pathlib.Path(sys.argv[1]).resolve(strict=True)
with os.add_dll_directory(str(p.parent)):
    lib=ctypes.CDLL(str(p),winmode=0x1100)
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.GetModuleFileNameW.argtypes=[ctypes.c_void_p,ctypes.c_wchar_p,ctypes.c_uint]
    kernel.GetModuleFileNameW.restype=ctypes.c_uint
    buf=ctypes.create_unicode_buffer(32768)
    n=kernel.GetModuleFileNameW(lib._handle,buf,len(buf))
    assert 0<n<len(buf) and pathlib.Path(buf.value).resolve()==p
    args=json.loads(sys.argv[3])
    fn=getattr(lib,sys.argv[2])
    fn.argtypes=[ctypes.c_int]*len(args)
    fn.restype=ctypes.c_char_p
    value=fn(*args)
    assert value
    print(json.dumps({'loadedPath':str(p),'value':value.decode('ascii')}))
'''
    process = subprocess.run([sys.executable, '-I', '-c', code, str(path), symbol,
                              json.dumps(arguments)], text=True,
                             capture_output=True, timeout=30)
    if process.returncode:
        raise ValueError('Native dependency probe failed: ' + process.stderr[-2000:])
    result = json.loads(process.stdout)
    if not isinstance(result.get('value'), str) or len(result['value']) > 2000:
        raise ValueError('Invalid dependency version response')
    return result


def check_value(value, expected, prefix):
    if not (value.startswith(expected) if prefix else value == expected):
        raise ValueError('Dependency version mismatch: ' + expected + ' versus ' + value)


def metadata(root):
    command = r'''
$ErrorActionPreference = 'Stop'
$rows = @(Get-ChildItem -LiteralPath $env:MWNET_PACKAGE_AUDIT_ROOT -Recurse -File -Filter '*.dll' | ForEach-Object {
    $v = $_.VersionInfo
    [PSCustomObject]@{path=$_.FullName;fileVersion=$v.FileVersion;productVersion=$v.ProductVersion;originalFilename=$v.OriginalFilename;companyName=$v.CompanyName}
})
ConvertTo-Json -InputObject $rows -Depth 3 -Compress
'''
    environment = dict(os.environ, MWNET_PACKAGE_AUDIT_ROOT=str(root))
    process = subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive',
                              '-Command', command], env=environment, check=True,
                             capture_output=True, text=True, timeout=60)
    rows = json.loads(process.stdout)
    if not isinstance(rows, list):
        raise ValueError('Expected a DLL metadata array')
    result = []
    for row in rows:
        path = Path(row.pop('path')).resolve(strict=True)
        if not path.is_relative_to(root):
            raise ValueError('DLL metadata escaped the package')
        result.append(dict(path=str(path.relative_to(root)), sha256=sha(path), **row))
    actual = {str(p.relative_to(root)) for p in root.rglob('*.dll')}
    if len(result) != len(actual) or {row['path'] for row in result} != actual:
        raise ValueError('Incomplete or duplicate DLL metadata inventory')
    return sorted(result, key=lambda row: row['path'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--sha256', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != 'win32':
        parser.error('This audit requires native Windows')
    configure_temp_environment()
    if args.output.exists():
        parser.error('Refusing to overwrite dependency evidence')
    args.output.mkdir(parents=True)
    report = {'status': 'running', 'archiveSha256': args.sha256,
              'scope': 'Preserved portable package metadata and six native dependency version APIs only; no rebuild, installer, graphical or distribution acceptance',
              'dlls': [], 'probes': [], 'distributionAccepted': False,
              'graphicalGameplayAccepted': False}
    try:
        root = extract_checked(args.archive, args.sha256, args.output / 'package')
        report['dlls'] = metadata(root)
        for filename, symbol, arguments, expected, prefix in PROBES:
            paths = list(root.rglob(filename))
            if len(paths) != 1:
                raise ValueError('Expected exactly one dependency: ' + filename)
            row = {'path': str(paths[0].relative_to(root)), 'sha256': sha(paths[0]),
                   'symbol': symbol, 'expected': expected, 'status': 'running'}
            report['probes'].append(row)
            value = native_probe(paths[0], symbol, arguments)
            row['runtimeValue'] = value['value']
            check_value(value['value'], expected, prefix)
            row['status'] = 'pass'
        report['status'] = 'pass'
    except Exception as error:
        report['status'] = 'failed'
        report['error'] = type(error).__name__ + ': ' + str(error)
        raise
    finally:
        with (args.output / 'dependencies.json').open('x', encoding='utf-8') as stream:
            json.dump(report, stream, indent=2)
            stream.write('\n')


if __name__ == '__main__':
    main()
