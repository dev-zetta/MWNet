#!/usr/bin/env python3
"""Prepare a private, verified content-only upgrade of the NUC registration game."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zlib

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
NAMES = ('Morrowind.esm', 'Tribunal.esm', 'Bloodmoon.esm')


def sha256(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def prepare(data, output, receipt):
    allowed = json.loads((ROOT / 'files/mwnet/core-scripts/defaults/requiredDataFiles.json').read_text())
    allowed = {name: {int(value, 16) for value in hashes} for item in allowed for name, hashes in item.items()}
    files = {}
    for name in NAMES:
        path = data / name
        if not path.is_file() or not 0 < path.stat().st_size <= 512 * 1024 * 1024:
            raise ValueError('Missing or oversized content file: ' + name)
        checksum = 0
        with path.open('rb') as stream:
            if stream.read(4) != b'TES3':
                raise ValueError('Not a TES3 content file: ' + name)
            stream.seek(0)
            while chunk := stream.read(1024 * 1024):
                checksum = zlib.crc32(chunk, checksum)
        if checksum not in allowed[name]:
            raise ValueError('Content does not match the current allowed CRC32 list: ' + name)
        files[name] = {'size': path.stat().st_size, 'sha256': sha256(path), 'crc32': f'{checksum:08X}'}
    if receipt['outcome'] != 'pass' or receipt['gameplayProtocol'] != 13:
        raise ValueError('A successful protocol 13 registration receipt is required')
    output.mkdir(mode=0o700)
    content = output / 'content'
    content.mkdir(mode=0o755)
    for name in NAMES:
        target = content / name
        shutil.copyfile(data / name, target)
        target.chmod(0o644)
        if sha256(target) != files[name]['sha256']:
            raise ValueError('Content changed during preparation: ' + name)
    for name in ('compose.content.yml', 'enable-gameplay.sh'):
        shutil.copy2(HERE / name, output / name)
    manifest = {'files': files, 'fingerprint': receipt['fingerprint'],
                'imageConfigDigest': receipt['imageConfigDigest'],
                'origin': receipt['origin'], 'endpoint': receipt['gameEndpoint']}
    (output / 'content.json').write_text(json.dumps(manifest, indent=2) + '\n')
    inputs = sorted(p for p in output.rglob('*') if p.is_file())
    (output / 'SHA256SUMS').write_text(''.join(sha256(p) + '  ' + str(p.relative_to(output)) + '\n' for p in inputs))
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--registration-receipt', type=Path,
                        default=ROOT / 'docs/release-evidence/2026-10-29-public-registration.json')
    args = parser.parse_args()
    manifest = prepare(args.data_dir, args.output, json.loads(args.registration_receipt.read_text()))
    print('Prepared private content bundle:', args.output)
    print('Verified ordered content files:', ', '.join(manifest['files']))


if __name__ == '__main__':
    main()
