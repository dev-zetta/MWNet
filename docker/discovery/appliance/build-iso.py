#!/usr/bin/env python3
"""Build a credential-free, interactive Ubuntu Server installer with MWNet payload.

Requires Python3/PyYAML, Docker/Buildx, xorriso, gpgv and Ubuntu's archive keyring.
Run as your build user with Docker access; never mounts or writes a physical disk.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile

import yaml


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
KEYRING = Path('/usr/share/keyrings/ubuntu-archive-keyring.gpg')
TUNNEL_FILES = ('compose.playit-agent.yml', 'compose.playit.yml', 'playit.env.example', 'playit.md',
                'update-playit.sh', 'update-host-and-playit.sh')


def host_release(iso):
    match = re.fullmatch(r'ubuntu-(24\.04|26\.04)(?:\.\d+)*-live-server-amd64\.iso', iso.name)
    if not match:
        raise ValueError('Expected an Ubuntu 24.04 or 26.04 live-server-amd64 ISO filename')
    return match[1]


def run(*args, **kwargs):
    return subprocess.run([str(arg) for arg in args], check=True, **kwargs)


def output(*args):
    return run(*args, capture_output=True, text=True).stdout.strip()


def digest(path, algorithm='sha256'):
    hasher = hashlib.new(algorithm)
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            hasher.update(chunk)
    return hasher.hexdigest()


def verify_base(iso, sums, signature, keyring):
    host_release(iso)
    run('gpgv', '--keyring', keyring, signature, sums)
    matches = []
    for line in sums.read_text().splitlines():
        match = re.fullmatch(r'([0-9a-f]{64}) [ *](.+)', line)
        if match and match[2] == iso.name:
            matches.append(match[1])
    if len(matches) != 1 or digest(iso) != matches[0]:
        raise ValueError('Base ISO does not match its signed checksum')
    return matches[0]


def source_context(destination):
    # Only explicit tracked build inputs; no .env, identities, accounts or local state.
    names = output('git', '-C', ROOT, 'ls-files', '-z', '--',
                   'CMakeLists.txt', 'cmake', 'components', 'apps', 'LICENSE',
                   'docker/discovery/Dockerfile').split('\0')
    hasher = hashlib.sha256()
    for name in sorted(filter(None, names)):
        source = ROOT / name
        if (source.name == '.env' or source.name.startswith('.env.')
                or source.suffix in ('.key', '.pem')
                or source.name in ('id_rsa', 'id_ed25519', 'id_ecdsa')):
            raise ValueError(f'Potential credential in build inputs: {name}')
        if source.is_symlink() or not source.is_file():
            raise ValueError(f'Build input must be a regular file: {name}')
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        hasher.update(name.encode() + b'\0' + bytes.fromhex(digest(target)))
    return hasher.hexdigest()


def runtime_compose(image):
    config = yaml.safe_load((HERE.parent / 'compose.yml').read_text())
    config['services']['directory'].pop('build')
    config['services']['directory']['image'] = image
    # No image downloads on appliance activation; all content is in the ISO archive.
    for service in config['services'].values():
        service['pull_policy'] = 'never'
    return config


def copy_tunnel_files(destination):
    # Explicit templates only: never collect a deployed agent.env or its secrets.
    for name in TUNNEL_FILES:
        source = HERE.parent / name
        if source.is_symlink() or not source.is_file():
            raise ValueError(f'Tunnel input must be a regular file: {name}')
        shutil.copy2(source, destination / name)


def build(args):
    iso, result = args.base_iso.resolve(), args.output.resolve()
    if result.exists() or result.with_suffix('.json').exists() or result.with_suffix('.iso.sha256').exists():
        raise ValueError('Output or its manifest/checksum already exists; choose a new output name')
    if result.suffix != '.iso':
        raise ValueError('Output must end in .iso')
    base_hash = verify_base(iso, args.checksums.resolve(), args.signature.resolve(), args.keyring.resolve())
    result.parent.mkdir(parents=True, exist_ok=True)
    # Large build/archive/remaster data stays next to output on disk, not in /tmp.
    with tempfile.TemporaryDirectory(prefix='.iso-build-', dir=result.parent) as temporary:
        stage = Path(temporary)
        context, payload = stage / 'source', stage / 'mwnet'
        context.mkdir()
        payload.mkdir()
        source_hash = source_context(context)
        revision = output('git', '-C', ROOT, 'rev-parse', 'HEAD')
        image = f'mwnet-directory:appliance-{source_hash[:16]}'
        run('docker', 'build', '--progress=plain', '--platform', 'linux/amd64', '--file',
            context / 'docker/discovery/Dockerfile', '--target', 'runtime',
            '--build-arg', f'SOURCE_REVISION={revision}', '--tag', image, context)
        compose = runtime_compose(image)
        caddy = compose['services']['https']['image']
        if '@sha256:' not in caddy:
            raise ValueError('Caddy image must be pinned by digest')
        run('docker', 'pull', '--platform', 'linux/amd64', caddy)
        # docker save/load does not consistently restore registry RepoDigests. Give
        # the verified Caddy image a local content-derived tag and lock its image ID.
        caddy_id = output('docker', 'image', 'inspect', '--format', '{{.Id}}', caddy)
        caddy_local = f'mwnet-caddy:appliance-{caddy_id.split(":")[1][:16]}'
        run('docker', 'tag', caddy, caddy_local)
        compose['services']['https']['image'] = caddy_local
        (payload / 'compose.yml').write_text(yaml.safe_dump(compose, sort_keys=False))
        references = [image, caddy_local]
        image_ids = {reference: output('docker', 'image', 'inspect', '--format', '{{.Id}}', reference)
                     for reference in references}
        (payload / 'images.lock').write_text(''.join(f'{ref} {ident}\n' for ref, ident in image_ids.items()))
        run('docker', 'save', '--output', payload / 'images.tar', *references)
        for name in ('first-boot.sh', 'mwnet-appliance-setup.service', 'mwnet-appliance-setup.timer', 'README.md'):
            shutil.copy2(HERE / name, payload / name)
        for name in ('setup-host.sh', 'Caddyfile', '.env.example'):
            shutil.copy2(HERE.parent / name, payload / name)
        copy_tunnel_files(payload)
        shutil.copy2(ROOT / 'docs/public-discovery.md', payload / 'public-discovery.md')
        with tarfile.open(payload / 'directory-source.tar.gz', 'w:gz') as archive:
            archive.add(context, arcname='source')
        manifest = {
            'format': 1, 'base_iso': iso.name, 'base_sha256': base_hash,
            'host_release': host_release(iso), 'host_architecture': 'amd64',
            'source_revision': revision, 'build_inputs_sha256': source_hash,
            'images': image_ids, 'caddy_registry_reference': caddy,
            'installation': 'interactive; internet required for host packages on first boot',
            'credentials_embedded': False, 'public_deployment_enabled': False,
        }
        (payload / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        (payload / 'payload.sha256').write_text(''.join(
            f'{digest(path)}  {path.name}\n' for path in sorted(payload.iterdir()) if path.is_file()))
        # Keep the stock installer integrity check useful after adding our files.
        md5 = stage / 'md5sum.txt'
        run('xorriso', '-osirrox', 'on', '-indev', iso, '-extract', '/md5sum.txt', md5)
        md5.chmod(0o644)  # xorriso preserves the installation medium's read-only mode.
        with md5.open('a') as stream:
            stream.write('\n')
            stream.write(f'{digest(HERE / "autoinstall.yaml", "md5")}  ./autoinstall.yaml\n')
            for path in sorted(payload.iterdir()):
                stream.write(f'{digest(path, "md5")}  ./mwnet/{path.name}\n')
        partial = stage / 'result.iso'
        run('xorriso', '-indev', iso, '-outdev', partial,
            '-map', HERE / 'autoinstall.yaml', '/autoinstall.yaml',
            '-map', payload, '/mwnet', '-map', md5, '/md5sum.txt',
            '-boot_image', 'any', 'replay')
        # Preserve an inspection report alongside the ISO without assuming that a
        # successful remaster proves boot or installed-host acceptance.
        report = run('xorriso', '-indev', partial, '-report_el_torito', 'plain',
                     '-report_system_area', 'plain', capture_output=True, text=True)
        manifest['boot_report'] = report.stdout + report.stderr
        manifest['iso_sha256'] = digest(partial)
        manifest['iso_bytes'] = partial.stat().st_size
        partial.rename(result)
        result.with_suffix('.json').write_text(json.dumps(manifest, indent=2) + '\n')
        result.with_suffix('.iso.sha256').write_text(f'{manifest["iso_sha256"]}  {result.name}\n')
    print(f'Created {result}\nSHA256 {manifest["iso_sha256"]}', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base-iso', type=Path, required=True)
    parser.add_argument('--checksums', type=Path, required=True, help='Official SHA256SUMS')
    parser.add_argument('--signature', type=Path, required=True, help='Official SHA256SUMS.gpg')
    parser.add_argument('--keyring', type=Path, default=KEYRING)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        build(args)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'Build failed: {error}\n')


if __name__ == '__main__':
    main()
