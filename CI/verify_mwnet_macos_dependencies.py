#!/usr/bin/env python3
"""Read actual frozen Mac provider versions using separate native processes."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

from mwnet_temp import configure_temp_environment


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def check_response(stdout, library, expected, kind):
    lines = stdout.splitlines()
    if len(lines) != 3 or len(stdout) > 4000:
        raise ValueError('Invalid native provider response')
    if Path(lines[0]).resolve(strict=True) != library.resolve(strict=True):
        raise ValueError('Native provider path mismatch')
    if lines[1] != expected:
        raise ValueError('Native provider version mismatch: ' + expected + ' versus ' + lines[1])
    coverage = {'freetype': 'initialize-version-finish',
                'brotli': 'version-and-tiny-vector-decode'}.get(kind, 'version-api')
    if lines[2] != coverage:
        raise ValueError('Native provider functional coverage missing')
    return {'runtimeVersion': lines[1], 'loadedPath': lines[0], 'coverage': coverage}


def verify(package, archive, architecture, output, expectations):
    selected = expectations['packages'][architecture]
    if sha(archive) != selected['archiveSha256']:
        raise ValueError('Frozen original DMG hash mismatch')
    package = package.resolve(strict=True)
    frameworks = (package / 'Contents/Frameworks').resolve(strict=True)
    if package.name != 'MWNet.app' or not frameworks.is_relative_to(package):
        raise ValueError('Expected the actual MWNet application Frameworks directory')
    if output.exists():
        raise ValueError('Refusing to overwrite native dependency evidence')
    output.mkdir(parents=True)
    source = Path(__file__).with_name('mwnet_native_library_probe.c')
    report = {'status': 'running', 'runtimeSourceCommit': expectations['runtimeSourceCommit'],
              'archiveSha256': selected['archiveSha256'], 'architecture': architecture,
              'host': platform.platform(), 'controllerRevision': os.environ.get('GITHUB_SHA'),
              'verifierSha256': sha(Path(__file__)), 'probeSourceSha256': sha(source),
              'expectationsDataSha256': hashlib.sha256(json.dumps(expectations, sort_keys=True).encode()).hexdigest(),
              'scope': 'Delivered provider identity and bounded initialization/decode APIs only; separate generated controller, no game binary rebuild, package resigning or rendered gameplay',
              'probes': [], 'nativeProviderApisAccepted': False,
              'graphicalGameplayAccepted': False, 'distributionAccepted': False}
    try:
        contents = output / 'probe/Contents'
        binaries = contents / 'MacOS'
        binaries.mkdir(parents=True)
        # Honor the delivered libraries' @executable_path lookups without
        # changing their bytes, loader commands, signatures or read-only image.
        (contents / 'Frameworks').symlink_to(frameworks, target_is_directory=True)
        helper = binaries / 'mwnet-native-library-probe'
        environment = {k: v for k, v in os.environ.items() if not k.startswith('DYLD_')}
        compile_result = subprocess.run(['clang', '-std=c99', '-Wall', '-Wextra', '-Werror', '-O2',
                                         str(source), '-Wl,-rpath,@executable_path/../Frameworks', '-o', str(helper)],
                                        capture_output=True, text=True, timeout=60, env=environment)
        (output / 'probe-compile.log').write_text(compile_result.stdout + compile_result.stderr)
        if compile_result.returncode:
            raise ValueError('Native controller compile failed; see probe-compile.log')
        report['probeExecutableSha256'] = sha(helper)
        for item in selected['probes']:
            library = (frameworks / item['path']).resolve(strict=True)
            if not library.is_relative_to(frameworks) or sha(library) != item['sha256']:
                raise ValueError('Delivered library path/hash mismatch: ' + item['path'])
            row = dict(item, status='running')
            report['probes'].append(row)
            process = subprocess.run([str(helper.resolve()), str(library), item['kind']],
                                     capture_output=True, text=True, timeout=30, env=environment)
            row.update(exitCode=process.returncode, stdout=process.stdout[:4000], stderr=process.stderr[-4000:])
            if process.returncode:
                raise ValueError('Native library probe failed: ' + item['path'])
            row.update(check_response(process.stdout, library, item['expectedVersion'], item['kind']))
            if sha(library) != item['sha256']:
                raise ValueError('Library changed during native inspection')
            row['status'] = 'passed'
        report['nativeProviderApisAccepted'] = True
        report['status'] = 'passed'
    except Exception as error:
        report['status'] = 'failed'
        report['error'] = type(error).__name__ + ': ' + str(error)
        raise
    finally:
        with (output / 'dependencies.json').open('x', encoding='utf-8') as stream:
            stream.write(json.dumps(report, indent=2) + '\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', required=True, type=Path)
    parser.add_argument('--archive', required=True, type=Path)
    parser.add_argument('--architecture', required=True, choices=['arm64', 'x86_64'])
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if sys.platform != 'darwin' or platform.machine() != args.architecture:
        parser.error('Actual native Mac architecture must match the selected candidate')
    configure_temp_environment()
    expectations = json.loads(Path(__file__).with_name('mwnet_macos_provider_expectations.json').read_text())
    verify(args.package, args.archive, args.architecture, args.output, expectations)


if __name__ == '__main__':
    main()
