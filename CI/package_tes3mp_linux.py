#!/usr/bin/env python3
"""Package an explicitly identified Linux candidate build, without publishing it."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tarfile
import tempfile


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--server-only', action='store_true', help='Omit the game client and resources')
    parser.add_argument('--source-dir', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--sbom', type=Path, required=True, help='Source SPDX JSON generated for this commit')
    parser.add_argument('--commit', required=True, help='Full commit of the supplied build inputs')
    args = parser.parse_args()
    if not re.fullmatch(r'[0-9a-f]{40}', args.commit):
        parser.error('--commit must be a full Git commit ID')
    source, build = args.source_dir.resolve(), args.build_dir.resolve()
    binary = build / 'tes3mp-server'
    if not binary.is_file() or not os.access(binary, os.X_OK):
        parser.error('build directory must contain an executable tes3mp-server')
    version = re.search(r'#define TES3MP_VERSION "([\w.\-]+)"',
                        (source / 'components/openmw-mp/Version.hpp').read_text()).group(1)
    scope = 'server' if args.server_only else 'client-server'
    name = f'TES3MP-{version}-Linux-{platform.machine()}-{scope}-{args.commit[:10]}'
    args.output_dir.mkdir(parents=True, exist_ok=True)
    archive = args.output_dir / f'{name}.tar.gz'
    if archive.exists():
        parser.error(f'refusing to overwrite {archive}')
    dependencies = subprocess.run(['ldd', str(binary)], check=True, text=True,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT).stdout
    if 'not found' in dependencies:
        parser.error('server has unresolved runtime dependencies')
    if not args.server_only:
        client = build / 'tes3mp'
        if not client.is_file() or not os.access(client, os.X_OK):
            parser.error('full preview requires an executable tes3mp client')
        client_dependencies = subprocess.run(['ldd', str(client)], check=True, text=True,
                                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT).stdout
        if 'not found' in client_dependencies:
            parser.error('client has unresolved runtime dependencies')
        dependencies += '\nClient libraries:\n' + client_dependencies
    with tempfile.TemporaryDirectory(prefix='tes3mp-package-', dir=args.output_dir) as tmp:
        root = Path(tmp) / name
        (root / 'build').mkdir(parents=True)
        shutil.copy2(binary, root / 'build/tes3mp-server')
        for filename in ('tes3mp-server-default.cfg', 'defaults.bin'):
            shutil.copy2(build / filename, root / 'build' / filename)
        for filename in ('run-tes3mp-server.sh', 'LICENSE', 'DEPENDENCIES.md',
                         'RELEASE_GATES.md', 'LUA_API_COMPATIBILITY.md'):
            shutil.copy2(source / filename, root / filename)
        core_source = source / 'files/tes3mp/core-scripts'
        core_target = root / 'files/tes3mp/core-scripts'
        # Runtime data can contain accounts and world state, even in a clean tracked tree.
        shutil.copytree(core_source, core_target, ignore=shutil.ignore_patterns('data'))
        (core_target / 'data').mkdir()
        shutil.copy2(core_source / 'data/moderation.json', core_target / 'data/moderation.json')
        (root / 'docs').mkdir()
        shutil.copy2(source / 'docs/public-discovery.md', root / 'docs/public-discovery.md')
        if not args.server_only:
            shutil.copy2(client, root / 'build/tes3mp')
            shutil.copy2(build / 'tes3mp-client-default.cfg', root / 'build/tes3mp-client-default.cfg')
            shutil.copy2(build / 'openmw.cfg', root / 'build/openmw.cfg')
            shutil.copytree(build / 'resources', root / 'build/resources')
            for filename in ('run-tes3mp.sh', 'run-tes3mp-local.sh'):
                shutil.copy2(source / filename, root / filename)
            for filename in ('gamecontrollerdb.txt',):
                if (build / filename).exists():
                    shutil.copy2(build / filename, root / 'build' / filename)
        (root / 'runtime-dependencies.txt').write_text(dependencies)
        (root / 'PREVIEW.txt').write_text(
            'TES3MP Linux preview; not a stable release.\n'
            'Run ./run-tes3mp-server.sh from this extracted directory.\n'
            'The launcher binds to 127.0.0.1:25565 and stores state in .tes3mp-test.\n'
            'Keep that state directory between launches. Never distribute it.\n'
            'Shared libraries are NOT bundled; use a compatible build-system runtime\n'
            '(this candidate was validated on Ubuntu 24.04). See runtime-dependencies.txt.\n'
            'No game content is included. Empty content is only a server startup test;\n'
            'a playable server requires matching, legally obtained game content.\n'
            'Cross-platform builds, gameplay acceptance, independent security/legal\n'
            'review and the public beta remain release gates.\n')
        if not args.server_only:
            with (root / 'PREVIEW.txt').open('a') as instructions:
                instructions.write('Client and resources are included. Run ./run-tes3mp-local.sh\n'
                                   'and select your Morrowind Data Files to configure a local session.\n'
                                   'The client version command still reports the underlying OpenMW version.\n'
                                   'Graphical gameplay has not been certified by the package smoke test.\n')
        sbom = json.loads(args.sbom.read_text())
        if not any(p.get('name') == 'TES3MP' and p.get('downloadLocation', '').endswith('@' + args.commit)
                   for p in sbom.get('packages', [])):
            parser.error('source SBOM must identify the supplied commit')
        shutil.copy2(args.sbom, root / 'tes3mp-source.spdx.json')
        files = {str(p.relative_to(root)): sha256(p)
                 for p in sorted(root.rglob('*')) if p.is_file()}
        manifest = {'schemaVersion': 1, 'version': version, 'runtimeSourceCommit': args.commit,
                    'scope': 'linux-' + scope + '-preview',
                    'provenanceNote': 'Runtime commit supplied by builder; packaged files may include later tooling or documentation. Hashes identify all packaged bytes.',
                    'packagerSha256': sha256(Path(__file__)),
                    'files': files}
        (root / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        temporary_archive = Path(tmp) / archive.name
        with tarfile.open(temporary_archive, 'w:gz') as output:
            output.add(root, arcname=name)
        shutil.move(temporary_archive, archive)
    archive.with_suffix(archive.suffix + '.sha256').write_text(f'{sha256(archive)}  {archive.name}\n')
    print(archive)


if __name__ == '__main__':
    main()
