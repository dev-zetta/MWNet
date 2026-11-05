#!/usr/bin/env python3
"""Add notices to copies of pinned Mac apps, sign and verify a separate ZIP.

Original DMGs are mounted read-only. This never rebuilds runtime code, creates
a disk image, requests a signing identity or claims graphical/notarized status.
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import stat
import struct
import subprocess
import sys

from inspect_mwnet_macos_payloads import macho_payloads, region, unpack
from mwnet_temp import configure_temp_environment


RUNTIME = 'a6868387dc80dcafd9b8ebad3d8c708511d9a615'
APPS = ('MWNet.app', 'OpenMW-CS.app')
RESOURCE_NAMES = ('THIRD_PARTY_NOTICES.txt', 'notice-materials.json', 'source-inputs.json', 'SOURCE_MATERIALS.txt')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def file_digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def signature_invariant(data):
    """Hash all slice bytes except signing blobs and their size metadata.

Unlike SDK section matching, this also protects raw load commands, symbol and
string tables, relocations, padding and every other unsigned byte. Only the
code-signature blob/offset/size and LINKEDIT vm/file sizes may change.
    """
    payloads = macho_payloads(data)  # Validate all slice/section bounds first.
    if data[:4] == b'\xcf\xfa\xed\xfe':
        slices = [data]
    else:
        wide = data[:4] == b'\xca\xfe\xba\xbf'
        fmt, stride = ('>IIQQII', 32) if wide else ('>5I', 20)
        slices = [region(data, values[2], values[3]) for values in
                  (unpack(fmt, data, 8 + index * stride) for index in range(len(payloads)))]
    results = []
    for source, payload in zip(slices, payloads):
        normalized = bytearray(source)
        commands, command_bytes = unpack('<II', source, 16)
        offset, signature = 32, None
        for _ in range(commands):
            command, size = unpack('<II', source, offset)
            if command == 0x1d:  # LC_CODE_SIGNATURE
                if signature is not None or size != 16:
                    raise ValueError('Ambiguous Mach-O signing command')
                start, length = unpack('<II', source, offset + 8)
                if start < 32 + command_bytes or not length:
                    raise ValueError('Invalid signing blob region')
                region(source, start, length)
                signature = (start, start + length)
                normalized[offset + 8:offset + 16] = b'\0' * 8
            elif command == 0x19 and source[offset + 8:offset + 24].rstrip(b'\0') == b'__LINKEDIT':
                # Retain vmaddr/fileoff/protections/flags; only the enclosing
                # region lengths change when the signing blob is replaced.
                struct.pack_into('<Q', normalized, offset + 32, 0)
                struct.pack_into('<Q', normalized, offset + 48, 0)
            offset += size
        if signature is None:
            raise ValueError('Expected an already signed original executable')
        start, end = signature
        results.append({'cpu': payload['cpu'], 'subtype': payload['subtype'],
                        'unsignedBytesSha256': digest(normalized[:start] + normalized[end:]),
                        'sectionPayloadSha256': payload['sectionPayloadSha256'],
                        'installName': payload['installName'], 'linkedLibraries': payload['linkedLibraries'],
                        'minimumOsVersions': payload['minimumOsVersions']})
    return results


def inventory(root):
    result = {}
    for path in sorted(root.rglob('*')):
        relative = path.relative_to(root).as_posix()
        status = path.lstat()
        row = {'mode': stat.S_IMODE(status.st_mode)}
        if path.is_symlink():
            target = os.readlink(path)
            if Path(target).is_absolute() or not path.resolve().is_relative_to(root.resolve()):
                raise ValueError('App link leaves its copied app: ' + relative)
            row.update(type='symlink', target=target)
        elif path.is_dir():
            row.update(type='directory')
        elif path.is_file():
            row.update(type='file', bytes=status.st_size, sha256=file_digest(path))
        else:
            raise ValueError('Unexpected app object: ' + relative)
        result[relative] = row
    return result


def check_changes(before, after, executable):
    main = 'Contents/MacOS/' + executable
    if before.get(main, {}).get('type') != 'file' or after.get(main, {}).get('type') != 'file':
        raise ValueError('App executable must remain a regular file')
    additions = {'Contents/Resources/distribution'} | {'Contents/Resources/distribution/' + x for x in RESOURCE_NAMES}
    changed, added, removed = [], [], []
    for name in sorted(set(before) | set(after)):
        if before.get(name) == after.get(name):
            continue
        signing = name == 'Contents/_CodeSignature' or name.startswith('Contents/_CodeSignature/')
        if name not in before:
            if name not in additions and not signing:
                raise ValueError('Unexpected candidate addition: ' + name)
            added.append(name)
        elif name not in after:
            if not signing:
                raise ValueError('Original payload removed: ' + name)
            removed.append(name)
        else:
            if name != main and not signing:
                raise ValueError('Original payload changed: ' + name)
            if name == main and before[name]['mode'] != after[name]['mode']:
                raise ValueError('Executable mode changed')
            changed.append(name)
    if not additions.issubset(after) or any(name in before for name in additions):
        raise ValueError('Notice additions are missing or overwrite original content')
    if (after['Contents/Resources/distribution']['type'] != 'directory'
            or any(after['Contents/Resources/distribution/' + name]['type'] != 'file' for name in RESOURCE_NAMES)):
        raise ValueError('Notice resources must be regular files within their new directory')
    return {'changedSigningPaths': changed, 'addedPaths': added, 'removedSigningPaths': removed,
            'unchangedOriginalFileCount': sum(row['type'] == 'file' and after.get(name) == row for name, row in before.items())}


def execute(argv, log):
    with log.open('ab') as stream:
        result = subprocess.run([str(x) for x in argv], stdout=stream, stderr=subprocess.STDOUT, timeout=180)
    if result.returncode:
        raise RuntimeError('Native command failed (' + str(result.returncode) + '): ' + str(argv[0]))


def prepare(args):
    if sys.platform != 'darwin':
        raise ValueError('Requires an actual native macOS host')
    configure_temp_environment()
    if os.uname().machine != args.architecture:
        raise ValueError('Native host architecture differs from the selected candidate')
    if args.runtime != RUNTIME or not re.fullmatch('[0-9a-f]{64}', args.dmg_sha256):
        raise ValueError('Unexpected runtime/archive identity')
    if subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip() != RUNTIME:
        raise ValueError('Runtime checkout differs from the pinned source')
    if file_digest(args.dmg) != args.dmg_sha256:
        raise ValueError('Original DMG hash differs from its pin')
    if args.output.exists():
        raise ValueError('Refusing to overwrite candidate/evidence')
    args.output.mkdir(parents=True)
    report = {'status': 'preparing', 'runtimeSourceCommit': RUNTIME, 'procedureSha256': file_digest(Path(__file__)),
              'originalDmg': {'sha256': args.dmg_sha256, 'bytes': args.dmg.stat().st_size},
              'host': {'architecture': os.uname().machine, 'macos': subprocess.check_output(['sw_vers', '-productVersion'], text=True).strip()},
              'scope': 'Separate notice-resource ZIP/app candidate with enclosing ad-hoc signatures; original runtime and DMG preserved',
              'graphicalAccepted': False, 'fullDistributionAccepted': False, 'notarized': False, 'published': False, 'apps': []}
    mounted = args.output / 'mounted'
    mounted.mkdir()
    attached = False
    try:
        manifest = json.loads((args.notices / 'candidate-resources.json').read_text())
        if (manifest.get('schemaVersion') != 1 or manifest['runtimeSourceCommit'] != RUNTIME
                or set(manifest['files']) != set(RESOURCE_NAMES)):
            raise ValueError('Unexpected notice-resource selection')
        for name, expected in manifest['files'].items():
            path = args.notices / name
            if path.is_symlink() or file_digest(path) != expected['sha256'] or path.stat().st_size != expected['bytes']:
                raise ValueError('Notice resource hash/size differs from its pin')
        report['resourceManifestSha256'] = file_digest(args.notices / 'candidate-resources.json')
        report['sourceMaterialCompanion'] = manifest['sourceMaterialCompanion']
        log = args.output / 'native-assembly.log'
        execute(['hdiutil', 'verify', args.dmg], log)
        execute(['hdiutil', 'attach', args.dmg, '-readonly', '-nobrowse', '-mountpoint', mounted.resolve()], log)
        attached = True
        if {p.name for p in mounted.glob('*.app')} != set(APPS):
            raise ValueError('Original app selection differs from the preserved image')
        directory_name = 'MWNet-1.0.0-macOS-' + args.architecture + '-native-candidate-notices-v3'
        candidate = args.output / directory_name
        candidate.mkdir()
        for app_name in APPS:
            original, copied = mounted / app_name, candidate / app_name
            execute(['codesign', '--verify', '--deep', '--strict', original], log)
            before = inventory(original)
            info = plistlib.loads((original / 'Contents/Info.plist').read_bytes())
            executable = info['CFBundleExecutable']
            if not re.fullmatch('[A-Za-z0-9_.-]+', executable):
                raise ValueError('Unexpected app executable name')
            invariant = signature_invariant((original / 'Contents/MacOS' / executable).read_bytes())
            execute(['ditto', original, copied], log)
            if inventory(copied) != before:
                raise ValueError('Copy does not preserve original app payload/modes/links')
            resources = copied / 'Contents/Resources/distribution'
            resources.mkdir()
            for name in RESOURCE_NAMES:
                execute(['ditto', args.notices / name, resources / name], log)
            execute(['codesign', '--force', '--sign', '-', '--timestamp=none',
                     '--preserve-metadata=identifier,entitlements,requirements', copied], log)
            execute(['codesign', '--verify', '--deep', '--strict', copied], log)
            after = inventory(copied)
            changes = check_changes(before, after, executable)
            final_invariant = signature_invariant((copied / 'Contents/MacOS' / executable).read_bytes())
            if invariant != final_invariant:
                raise ValueError('Executable unsigned bytes or loader identity changed')
            (args.output / (app_name + '-original-inventory.json')).write_text(json.dumps(before, indent=2) + '\n')
            (args.output / (app_name + '-candidate-inventory.json')).write_text(json.dumps(after, indent=2) + '\n')
            report['apps'].append({'app': app_name, 'executable': executable, **changes,
                                   'executableUnsignedIdentity': invariant, 'enclosingAdHocSignatureVerified': True})
        archive = args.output / (directory_name + '.zip')
        execute(['ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', candidate, archive], log)
        extracted = args.output / 'zip-readback'
        extracted.mkdir()
        execute(['ditto', '-x', '-k', archive, extracted], log)
        final_root = extracted / directory_name
        for app_name in APPS:
            app = final_root / app_name
            if inventory(app) != inventory(candidate / app_name):
                raise ValueError('ZIP readback differs from candidate payload/modes/links')
            execute(['codesign', '--verify', '--deep', '--strict', app], log)
        execute([sys.executable, Path(__file__).parent / 'smoke_mwnet_package.py', final_root / 'MWNet.app', '--commit', RUNTIME],
                args.output / 'native-final-package.log')
        if file_digest(args.dmg) != args.dmg_sha256:
            raise ValueError('Original DMG changed')
        report.update(status='prepared-and-native-final-checks-passed', checkedUtc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                      candidateZip={'name': archive.name, 'bytes': archive.stat().st_size, 'sha256': file_digest(archive)},
                      originalDmgUnchanged=True, zipReadbackPayloadModeLinkEquality=True,
                      finalClientServerIdentityAndCoreScriptsCleanExit=True)
    except Exception as error:
        report.update(status='failed', error=type(error).__name__ + ': ' + str(error))
        raise
    finally:
        if attached:
            try:
                execute(['hdiutil', 'detach', mounted.resolve()], args.output / 'native-assembly.log')
                report['originalImageDetached'] = True
            except Exception as error:
                report.update(status='failed', detachError=type(error).__name__ + ': ' + str(error))
        (args.output / 'candidate-result.json').write_text(json.dumps(report, indent=2) + '\n')
    if report['status'] != 'prepared-and-native-final-checks-passed':
        raise RuntimeError('Candidate did not complete all final checks')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dmg', type=Path, required=True)
    parser.add_argument('--dmg-sha256', required=True)
    parser.add_argument('--runtime', required=True)
    parser.add_argument('--architecture', choices=['arm64', 'x86_64'], required=True)
    parser.add_argument('--notices', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    result = prepare(parser.parse_args())
    print(json.dumps({'status': result['status'], 'candidateZip': result['candidateZip']}))


if __name__ == '__main__':
    main()
