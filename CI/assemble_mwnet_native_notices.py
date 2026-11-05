#!/usr/bin/env python3
"""Assemble verbatim notice material from pinned sources and exported SDKs.

The result is intentionally conservative. It preserves texts and source
references; component selection, license compatibility and complete binary
distribution acceptance remain separate assessments.
"""
from __future__ import annotations

import argparse
import bz2
import gzip
import hashlib
import json
import lzma
from pathlib import Path, PurePosixPath
import posixpath
import re
import shutil
import stat
import subprocess
import tarfile
import tempfile
import zipfile

from collect_mwnet_third_party_notices import SOURCE_NOTICES, source_notice
from mwnet_temp import configure_temp_environment


def digest(data):
    return hashlib.sha256(data).hexdigest()


def file_digest(path, algorithm='sha256'):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, algorithm).hexdigest()


def legal_member(name):
    path = PurePosixPath(name)
    base = path.name.casefold()
    if base == 'qt_attribution.json':
        return True
    if path.suffix.casefold() in {'.c', '.cc', '.cpp', '.h', '.hpp', '.pm', '.pem', '.crt', '.key', '.py', '.sh', '.pl'}:
        return False
    return (any(p.casefold() in {'license', 'licenses', 'licence', 'licences'} for p in path.parts[:-1])
            or bool(re.match(r'^(licen[cs]e|copying|copyrights?|notice|authors|credits|acknowledg(e)?ments?)([._ -]|$)', base))
            or base in {'ftl.txt', 'gplv2.txt', 'gplv3.txt'})


class SourceArchive:
    def __init__(self, path):
        self.zip = zipfile.is_zipfile(path)
        self.scratch = None
        if self.zip:
            self.handle = zipfile.ZipFile(path)
        else:
            with path.open('rb') as stream:
                magic = stream.read(6)
            opener = (gzip.open if magic.startswith(b'\x1f\x8b') else
                      lzma.open if magic.startswith(b'\xfd7zXZ\x00') else
                      bz2.open if magic.startswith(b'BZh') else None)
            if opener:
                # One disk-backed decompression avoids repeatedly inflating
                # large archives for parent-relative Qt license references.
                self.scratch = tempfile.TemporaryFile()
                with opener(path, 'rb') as stream:
                    shutil.copyfileobj(stream, self.scratch)
                self.scratch.seek(0)
                self.handle = tarfile.open(fileobj=self.scratch, mode='r:')
            else:
                self.handle = tarfile.open(path, mode='r:')
        self.members = {}
        for item in self.handle.infolist() if self.zip else self.handle.getmembers():
            regular = not item.is_dir() and not stat.S_ISLNK(item.external_attr >> 16) if self.zip else item.isfile()
            if not regular:
                continue
            raw = item.filename if self.zip else item.name
            name = posixpath.normpath(raw)
            if name.startswith('/') or name == '..' or name.startswith('../') or '\\' in name or name in self.members:
                raise ValueError('Unsafe or ambiguous source archive member')
            self.members[name] = item

    def read(self, name):
        item = self.members[name]
        size = item.file_size if self.zip else item.size
        if size > 2 * 1024 * 1024:
            raise ValueError('Notice/reference text exceeds its bound')
        if self.zip:
            data = self.handle.read(item)
        else:
            with self.handle.extractfile(item) as stream:
                data = stream.read()
        if b'\0' in data:
            raise ValueError('Notice/reference is not a text file')
        return data

    def close(self):
        self.handle.close()
        if self.scratch:
            self.scratch.close()


def archive_notices(path, origin):
    archive = SourceArchive(path)
    entries, references, unresolved, metadata_diagnostics = {}, [], [], []
    try:
        for name in sorted(archive.members):
            if legal_member(name):
                entries[name] = archive.read(name)
            if PurePosixPath(name).name.casefold() != 'qt_attribution.json':
                continue
            raw_metadata = archive.read(name)
            try:
                metadata = json.loads(raw_metadata)
            except json.JSONDecodeError as error:
                # Actual upstream Qt attribution files contain literal
                # newlines inside descriptive strings. Preserve those bytes
                # and record this narrow parsing accommodation explicitly.
                if not error.msg.startswith('Invalid control character'):
                    raise ValueError('Invalid Qt attribution metadata: ' + name) from error
                metadata = json.loads(raw_metadata, strict=False)
                metadata_diagnostics.append({'metadata': name, 'sha256': digest(raw_metadata),
                                             'parserAccommodation': 'literal control characters in JSON strings',
                                             'strictParserError': str(error)})
            for row in metadata if isinstance(metadata, list) else [metadata]:
                filenames = row.get('LicenseFile') or row.get('LicenseFiles') or []
                if isinstance(filenames, str):
                    filenames = [filenames]
                if not isinstance(filenames, list):
                    raise ValueError('Unexpected Qt license reference schema')
                for filename in filenames:
                    if not isinstance(filename, str):
                        raise ValueError('Unexpected Qt license reference value')
                    target = posixpath.normpath(posixpath.join(posixpath.dirname(name), filename))
                    reference = {'metadata': name, 'component': row.get('Name'), 'reference': filename,
                                 'resolvedMember': target}
                    # The archive's project root is never left, even when Qt
                    # uses parent-relative references to its LICENSES folder.
                    if (target.split('/')[0] != name.split('/')[0] or target not in archive.members):
                        unresolved.append(reference)
                        continue
                    data = archive.read(target)
                    entries[target] = data
                    references.append(dict(reference, sha256=digest(data)))
        return ([{'origin': origin + '::' + name, 'bytes': data} for name, data in entries.items()],
                references, unresolved, metadata_diagnostics)
    finally:
        archive.close()


def collect_sources(collections):
    unique = {}
    for collection in collections:
        receipt = json.loads((collection / 'source-delivery.json').read_text())
        if receipt.get('status') != 'complete-for-listed-inputs':
            raise ValueError('Source collection is not complete for its listed inputs')
        for row in receipt['sources']:
            path = collection / row['filename']
            if path.is_symlink() or path.name != row['filename']:
                raise ValueError('Unsafe source archive reference')
            actual = file_digest(path)
            if actual != row['sha256'] or file_digest(path, row['algorithm']) != row['checksum']:
                raise ValueError('Source archive pin changed')
            unique.setdefault(actual, dict(row, path=path))
    return list(unique.values())


def assemble(source_dir, runtime, collections, sdk_roots, extras, output):
    tool_sha256 = file_digest(Path(__file__))
    if output.exists():
        raise ValueError('Refusing to overwrite notice material')
    if not re.fullmatch('[0-9a-f]{40}', runtime):
        raise ValueError('Expected one frozen runtime commit')
    sources = collect_sources(collections)
    notices, resolved, unresolved, metadata_diagnostics = [], [], [], []
    for row in sources:
        if row.get('inputType') == 'patch' or row['filename'].endswith('.patch.gz'):
            continue
        found, checked, missing, diagnostics = archive_notices(row['path'], 'source:' + row['filename'] + '@sha256:' + row['sha256'])
        notices.extend(found)
        resolved.extend(dict(x, archive=row['filename']) for x in checked)
        unresolved.extend(dict(x, archive=row['filename']) for x in missing)
        metadata_diagnostics.extend(dict(x, archive=row['filename']) for x in diagnostics)
    for label, (path, mode) in SOURCE_NOTICES.items():
        data = subprocess.check_output(['git', '-C', str(source_dir), 'show', runtime + ':' + path])
        notices.append({'origin': 'runtime:' + runtime + ':' + path + ':' + label, 'bytes': source_notice(data, mode)})
    for path in ['LICENSE', 'AUTHORS.md', 'mwnet-credits.md']:
        data = subprocess.check_output(['git', '-C', str(source_dir), 'show', runtime + ':' + path])
        notices.append({'origin': 'runtime:' + runtime + ':' + path, 'bytes': data})
    manifest = json.loads((source_dir / 'docs/license/third-party/manifest.json').read_text())
    for row in manifest['entries']:
        data = (source_dir / 'docs/license/third-party' / row['licenseFile']).read_bytes()
        pinned = subprocess.check_output(['git', '-C', str(source_dir), 'show', runtime + ':' + row['sourcePinFile']])
        if row['sourcePin'].encode() not in pinned or digest(data) != row['sha256']:
            raise ValueError('Static/fetched source notice pin changed')
        notices.append({'origin': row['upstreamNotice'], 'bytes': data})
    for root in sdk_roots:
        for path in sorted(root.rglob('copyright')):
            if path.parent.parent.name != 'share':
                continue
            notices.append({'origin': 'exported-sdk:' + root.name + ':share/' + path.parent.name + '/copyright', 'bytes': path.read_bytes()})
    for path in extras:
        notices.append({'origin': 'recipe-license:' + path.name, 'bytes': path.read_bytes()})
    unique = {}
    for row in notices:
        key = digest(row['bytes'])
        unique.setdefault(key, {'bytes': row['bytes'], 'origins': []})['origins'].append(row['origin'])
    output.mkdir(parents=True)
    book = bytearray(b'MWNet native candidate third-party notice material\n\nVerbatim texts from the named pinned inputs follow. This conservative collection includes unshipped source/development components; it does not relicense them or itself certify distribution completeness. Main MWNet/OpenMW/TES3MP terms and credits are retained.\n')
    index = []
    for checksum, row in sorted(unique.items()):
        book.extend(('\n' + '=' * 78 + '\nSHA256: ' + checksum + '\n' + '\n'.join('Source: ' + x for x in sorted(set(row['origins']))) + '\n' + '=' * 78 + '\n\n').encode())
        book.extend(row['bytes'])
        book.extend(b'\n')
        index.append({'sha256': checksum, 'bytes': len(row['bytes']), 'origins': sorted(set(row['origins']))})
    (output / 'THIRD_PARTY_NOTICES.txt').write_bytes(book)
    report = {'schemaVersion': 1, 'runtimeSourceCommit': runtime, 'toolSha256': tool_sha256,
              'scope': 'Verbatim source/SDK/project/recipe notice material and explicit Qt license-reference resolution; not final binary/legal/gameplay acceptance',
              'status': 'assembled-with-open-qt-references' if unresolved else 'assembled-for-listed-inputs',
              'sourceInputCount': len(sources), 'noticeInputsBeforeDeduplication': len(notices),
              'uniqueNoticeTexts': len(index), 'noticeBookSha256': digest(book), 'noticeBookBytes': len(book),
              'noticeTexts': index, 'resolvedQtLicenseReferences': resolved, 'unresolvedQtLicenseReferences': unresolved,
              'upstreamMetadataParsingDiagnostics': metadata_diagnostics,
              'fullDistributionAccepted': False}
    (output / 'notice-materials.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, required=True)
    parser.add_argument('--runtime', required=True)
    parser.add_argument('--collection', type=Path, action='append', required=True)
    parser.add_argument('--sdk-root', type=Path, action='append', default=[])
    parser.add_argument('--extra-notice', type=Path, action='append', default=[])
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    configure_temp_environment()
    report = assemble(args.source_dir, args.runtime, args.collection, args.sdk_root, args.extra_notice, args.output)
    print(json.dumps({k: report[k] for k in ['status', 'sourceInputCount', 'uniqueNoticeTexts', 'noticeBookBytes']}))
    if report['unresolvedQtLicenseReferences']:
        parser.exit(1, 'Explicit Qt license references remain unresolved; retained in the material receipt\n')


if __name__ == '__main__':
    main()
