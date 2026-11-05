#!/usr/bin/env python3
"""Fetch checksum-pinned source archives without extracting or executing them.

The receipt proves delivery of the listed inputs. It does not infer which
components a binary includes or certify license/source completeness.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import time
import urllib.parse
import urllib.request

from mwnet_temp import configure_temp_environment

MAX_ARCHIVE_BYTES = 1024 * 1024 * 1024


def digest(path: Path, algorithm: str) -> str:
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, algorithm).hexdigest()


def validate(manifest: dict) -> list[dict]:
    if manifest.get('schemaVersion') != 1 or not isinstance(manifest.get('sources'), list) or not manifest['sources']:
        raise ValueError('Expected a versioned, nonempty source manifest')
    names = set()
    for row in manifest['sources']:
        name = row.get('filename', '')
        if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.+-]{0,180}', name) or name in names:
            raise ValueError('Unsafe or duplicate source filename')
        names.add(name)
        algorithm = row.get('algorithm')
        size = {'sha256': 64, 'sha512': 128}.get(algorithm)
        if size is None or not re.fullmatch('[0-9a-f]{' + str(size) + '}', row.get('checksum', '')):
            raise ValueError('Every source must have an explicit SHA256 or SHA512 pin')
        urls = row.get('urls')
        if not isinstance(urls, list) or not urls:
            raise ValueError('Every source needs a download URL')
        for url in urls:
            parsed = urllib.parse.urlsplit(url)
            if parsed.scheme != 'https' or not parsed.hostname or parsed.username or parsed.password or parsed.fragment:
                raise ValueError('Source URLs must be credential-free HTTPS')
    return manifest['sources']


def download(row: dict, destination: Path) -> None:
    errors = []
    for index, url in enumerate(row['urls']):
        partial = destination.with_name(destination.name + '.attempt-' + str(index) + '.partial')
        try:
            started = time.monotonic()
            request = urllib.request.Request(url, headers={'User-Agent': 'MWNet-source-delivery'})
            with urllib.request.urlopen(request, timeout=45) as response, partial.open('xb') as output:
                if not response.geturl().startswith('https://'):
                    raise ValueError('Refusing a non-HTTPS download redirect')
                count = 0
                while True:
                    data = response.read(1024 * 1024)
                    if not data:
                        break
                    count += len(data)
                    if count > MAX_ARCHIVE_BYTES or time.monotonic() - started > 600:
                        raise ValueError('Source download exceeded its size/time budget')
                    output.write(data)
            if digest(partial, row['algorithm']) != row['checksum']:
                raise ValueError('Source archive checksum mismatch')
            partial.rename(destination)
            return
        except (OSError, ValueError) as error:
            errors.append(type(error).__name__ + ': ' + str(error))
    raise ValueError('Unable to fetch pinned source ' + row['filename'] + ': ' + '; '.join(errors))


def collect(manifest: dict, cache: Path, output: Path, fetch=download) -> dict:
    rows = validate(manifest)
    if output.exists():
        raise ValueError('Output must be new; preserve previous source receipts')
    cache.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True)
    report = {'schemaVersion': 1, 'status': 'running',
              'scope': 'Exact listed source archives only; binary coverage and distribution acceptance require separate review',
              'sources': []}
    try:
        for row in rows:
            path = cache / row['filename']
            if path.is_symlink():
                raise ValueError('Refusing a source cache symlink')
            if not path.exists():
                fetch(row, path)
            if not path.is_file() or digest(path, row['algorithm']) != row['checksum']:
                raise ValueError('Cached source archive checksum mismatch: ' + row['filename'])
            shutil.copyfile(path, output / path.name)
            if digest(output / path.name, row['algorithm']) != row['checksum']:
                raise ValueError('Copied source archive checksum mismatch')
            report['sources'].append({**row, 'bytes': path.stat().st_size, 'sha256': digest(path, 'sha256')})
        report['status'] = 'complete-for-listed-inputs'
        return report
    except Exception as error:
        report.update(status='failed', error=type(error).__name__ + ': ' + str(error))
        raise
    finally:
        (output / 'source-delivery.json').write_text(json.dumps(report, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', required=True, type=Path)
    parser.add_argument('--cache', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    configure_temp_environment()
    manifest = json.loads(args.manifest.read_text())
    report = collect(manifest, args.cache.resolve(), args.output.resolve())
    print(json.dumps({'status': report['status'], 'sourceArchives': len(report['sources'])}))


if __name__ == '__main__':
    main()
