#!/usr/bin/env python3
"""Extract a hash-verified server-only subset of the validated alpha.2 package."""
import argparse
import hashlib
import ipaddress
import json
from pathlib import Path, PurePosixPath
import shutil
import tarfile

HERE = Path(__file__).resolve().parent
RELEASE_SHA256 = 'c2babddf4f06b4989e7790f5732073da17d71b74e70fd9abd9cb7745fec09a7d'
RELEASE_COMMIT = '4a92d877b45a937686e38b8c3b55aab4964c9584'


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--archive', required=True, type=Path)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--public-address', required=True)
    p.add_argument('--public-port', required=True, type=int)
    args = p.parse_args()
    if not ipaddress.ip_address(args.public_address).is_global or not 1 <= args.public_port <= 65535:
        p.error('A public numeric address and UDP port are required')
    if digest(args.archive) != RELEASE_SHA256:
        p.error('Archive is not the validated alpha.2 Linux release')
    if args.output.exists():
        p.error('Output already exists; select a new directory')
    args.output.mkdir(parents=True)
    package = args.output / 'package'
    package.mkdir()
    with tarfile.open(args.archive) as archive:
        members = archive.getmembers()
        manifest_member = next(m for m in members if m.name.endswith('/manifest.json'))
        manifest = json.load(archive.extractfile(manifest_member))
        if (manifest['runtimeSourceCommit'] != RELEASE_COMMIT or manifest['protocol'] != 13
                or manifest['version'] != '1.0.0-alpha.2'):
            raise ValueError('Unexpected package identity')
        prefix = manifest_member.name.rsplit('/', 1)[0] + '/'
        selected = {}
        for member in members:
            if not member.name.startswith(prefix):
                continue
            name = member.name[len(prefix):]
            wanted = (name in {'build/mwnet-server', 'build/mwnet-server-default.cfg', 'build/defaults.bin',
                              'run-mwnet-server.sh', 'LICENSE', 'AUTHORS.md', 'DEPENDENCIES.md',
                              'mwnet-credits.md', 'mwnet-source.spdx.json'}
                      or name.startswith('files/mwnet/core-scripts/'))
            if not wanted or member.isdir():
                continue
            path = PurePosixPath(name)
            if not member.isfile() or path.is_absolute() or '..' in path.parts:
                raise ValueError('Unsafe selected archive member')
            if name.startswith('files/mwnet/core-scripts/data/') and name != 'files/mwnet/core-scripts/data/moderation.json':
                raise ValueError('Package contains local account/world state')
            data = archive.extractfile(member).read()
            if hashlib.sha256(data).hexdigest() != manifest['files'][name]:
                raise ValueError('Package file hash mismatch: ' + name)
            target = package / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            target.chmod(0o755 if name in ('build/mwnet-server', 'run-mwnet-server.sh') else 0o644)
            selected[name] = manifest['files'][name]
    for name in ('Dockerfile', 'compose.yml', 'entrypoint.py', 'verify_listing.py', 'run-test.sh'):
        shutil.copy2(HERE / name, args.output / name)
    (args.output / '.env').write_text(f'PUBLIC_ADDRESS={args.public_address}\nPUBLIC_PORT={args.public_port}\nDISCOVERY_URL=https://mwnet.playit.plus\nANNOUNCE=true\n')
    (args.output / '.env').chmod(0o600)
    (args.output / 'source.json').write_text(json.dumps({'releaseCommit': RELEASE_COMMIT,
        'archiveSha256': RELEASE_SHA256, 'version': '1.0.0-alpha.2', 'protocol': 13, 'files': selected}, indent=2) + '\n')
    files = sorted(x for x in args.output.rglob('*') if x.is_file())
    (args.output / 'SHA256SUMS').write_text(''.join(digest(f) + '  ' + str(f.relative_to(args.output)) + '\n' for f in files))
    print('Prepared verified game-server test bundle:', args.output)


if __name__ == '__main__':
    main()
