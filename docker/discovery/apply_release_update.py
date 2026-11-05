#!/usr/bin/env python3
"""Apply a checksummed NUC code bundle with isolated startup checks and rollback."""
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone

import yaml
from release_overlay import game_base, game_overlay, directory_overlay


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def call(arguments, timeout=120, output=None):
    result = subprocess.run(arguments, stdout=subprocess.PIPE if output is None else output,
                            stderr=subprocess.PIPE, timeout=timeout)
    if result.returncode:
        raise RuntimeError('Command failed: ' + ' '.join(map(str, arguments[:3])))
    return result.stdout.decode() if output is None else ''


def inspect(container):
    return json.loads(call(['docker', '--context', 'default', 'inspect', container]))[0]


def atomic(path, data):
    attributes = path.stat()
    temporary = path.with_name('.' + path.name + '.release-new')
    with temporary.open('x') as stream:
        stream.write(data)
    temporary.chmod(0o600)
    os.chown(temporary, attributes.st_uid, attributes.st_gid)
    temporary.replace(path)


def wait_ready(container, marker=None):
    for _ in range(45):
        state = inspect(container)['State']
        if not state['Running'] or state['OOMKilled']:
            raise RuntimeError('Startup failed: ' + container)
        if marker:
            if marker in call(['docker', '--context', 'default', 'logs', container]):
                return
        elif subprocess.run(['docker', '--context', 'default', 'exec', container, 'curl', '--fail', '--silent',
                             'http://127.0.0.1:8080/healthz'], stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, timeout=5).returncode == 0:
            return
        time.sleep(2)
    raise RuntimeError('Startup deadline exceeded: ' + container)


def immutable_code(root):
    for path in root.rglob('*'):
        if path.is_symlink():
            raise ValueError('Release code must not contain symlinks')
        path.chmod(0o755 if path.is_dir() or path.name in ('mwnet-server', 'run-mwnet-server.sh') else 0o644)
    root.chmod(0o755)


def validate_manifest(manifest):
    if (not re.fullmatch('[0-9a-f]{40}', str(manifest.get('runtimeSourceCommit', '')))
            or not re.fullmatch('[0-9A-Za-z.+-]{1,64}', str(manifest.get('version', '')))
            or not re.fullmatch('ed25519/[A-Za-z0-9_-]{43}', str(manifest.get('fingerprint', '')))):
        raise ValueError('Invalid release identity')
    for key in ('previousGameBinarySha256', 'serverSha256', 'directorySha256'):
        if not re.fullmatch('[0-9a-f]{64}', str(manifest.get(key, ''))):
            raise ValueError('Invalid binary digest')
    if 'previousRuntimeSourceCommit' in manifest or 'previousDirectorySha256' in manifest:
        if (not re.fullmatch('[0-9a-f]{40}', str(manifest.get('previousRuntimeSourceCommit', '')))
                or not re.fullmatch('[0-9a-f]{64}', str(manifest.get('previousDirectorySha256', '')))):
            raise ValueError('Invalid previous release identity')
    images = manifest.get('acceptedGameImageIds')
    if (not isinstance(images, list) or not 1 <= len(images) <= 3
            or any(not isinstance(value, str) or not re.fullmatch('sha256:[0-9a-f]{64}', value) for value in images)):
        raise ValueError('Invalid accepted image digests')
    inputs = manifest.get('inputs')
    if not isinstance(inputs, dict) or not 1 <= len(inputs) <= 5000:
        raise ValueError('Invalid input inventory')
    for name, expected in inputs.items():
        path = Path(name)
        if path.is_absolute() or '..' in path.parts or not re.fullmatch('[0-9a-f]{64}', str(expected)):
            raise ValueError('Invalid release input path or digest')
    if (inputs.get('server-package/build/mwnet-server') != manifest['serverSha256']
            or inputs.get('mwnet-directory') != manifest['directorySha256']):
        raise ValueError('Binary identity differs from the input inventory')


def run(bundle):
    os.umask(0o077)
    if os.geteuid() != 0:
        raise ValueError('Interactive sudo authentication is required on the NUC')
    with Path('/run/lock/mwnet-gameplay.lock').open('w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        return apply_locked(bundle)


def apply_locked(bundle):
    manifest = json.loads((bundle / 'deployment.json').read_text())
    validate_manifest(manifest)
    for name, expected in manifest['inputs'].items():
        path = bundle / name
        if not path.resolve().is_relative_to(bundle) or path.is_symlink() or sha(path) != expected:
            raise ValueError('Release input changed: ' + name)
    game_file = Path('/etc/mwnet-gameplay/compose.content.yml')
    base_file = Path('/home/gmax/mwnet-registration/compose.yml')
    directory_file = Path('/opt/mwnet/discovery-appliance/compose.yml')
    game = ['docker', '--context', 'default', 'compose', '-p', 'mwnet-registration', '--env-file', '/home/gmax/mwnet-registration/.env',
            '--env-file', '/etc/mwnet-gameplay/content.env', '-f', '/home/gmax/mwnet-registration/compose.yml',
            '-f', str(game_file)]
    directory = ['docker', '--context', 'default', 'compose', '--env-file', '/etc/mwnet-directory.env',
                 '--project-directory', str(directory_file.parent),
                 '-p', 'mwnet-directory', '-f', str(directory_file)]
    game_old, directory_old = inspect('mwnet-registration-game-1'), inspect('mwnet-directory-directory-1')
    receipt = bundle / 'deployment-result.json'
    if receipt.exists():
        previous = json.loads(receipt.read_text())
        if (previous.get('outcome') == 'pass'
                and call(['docker', '--context', 'default', 'exec', game_old['Id'], 'sha256sum', '/opt/mwnet/build/mwnet-server']).split()[0] == manifest['serverSha256']
                and call(['docker', '--context', 'default', 'exec', directory_old['Id'], 'sha256sum', '/usr/local/bin/mwnet-directory']).split()[0] == manifest['directorySha256']):
            print('Already applied: matching runtime bytes; original deployment receipt retained.')
            return
        raise ValueError('Existing deployment receipt does not match the running code')
    if game_old['Image'] not in manifest['acceptedGameImageIds']:
        raise ValueError('The existing game base image differs from the reviewed deployment')
    if call(['docker', '--context', 'default', 'exec', game_old['Id'], 'sha256sum', '/opt/mwnet/build/mwnet-server']).split()[0] not in (manifest['previousGameBinarySha256'], manifest['serverSha256']):
        raise ValueError('The running game binary changed since update preparation')
    previous_release = None
    if 'previousRuntimeSourceCommit' in manifest:
        previous_release = '/srv/mwnet/releases/' + manifest['previousRuntimeSourceCommit']
        if call(['docker', '--context', 'default', 'exec', directory_old['Id'], 'sha256sum', '/usr/local/bin/mwnet-directory']).split()[0] not in (manifest['previousDirectorySha256'], manifest['directorySha256']):
            raise ValueError('The running directory binary changed since update preparation')
    content = next(m for m in game_old['Mounts'] if m['Destination'] == '/game-content')
    if content['RW']:
        raise ValueError('Canonical content must remain read-only')
    release = Path('/srv/mwnet/releases') / manifest['runtimeSourceCommit']
    filesystem = call(['stat', '-f', '-c', '%T', '/srv/mwnet']).strip()
    if filesystem in ('tmpfs', 'ramfs') or shutil.disk_usage('/srv/mwnet').free < 1024 ** 3:
        raise ValueError('Need persistent disk and at least1GiB free for code and backups')
    if release.exists():
        for name, expected in manifest['inputs'].items():
            if name.startswith('server-package/') or name in ('entrypoint.py', 'verify_listing.py', 'mwnet-directory'):
                if (release / name).is_symlink() or sha(release / name) != expected:
                    raise ValueError('Installed code differs from this bundle')
    else:
        release.mkdir(parents=True, mode=0o700)
        shutil.copytree(bundle / 'server-package', release / 'server-package')
        for name in ('entrypoint.py', 'verify_listing.py', 'mwnet-directory'):
            shutil.copyfile(bundle / name, release / name)
            (release / name).chmod(0o755 if name == 'mwnet-directory' else 0o644)
        immutable_code(release / 'server-package')
    (release / 'tmp').mkdir(mode=0o700, exist_ok=True)
    os.chown(release / 'tmp', 10001, 10001)
    (release / 'smoke-tmp').mkdir(mode=0o700, exist_ok=True)
    os.chown(release / 'smoke-tmp', 10001, 10001)
    next_base = game_base(yaml.safe_load(base_file.read_text()))
    next_game = game_overlay(yaml.safe_load(game_file.read_text()), str(release), previous_release)
    next_directory = directory_overlay(yaml.safe_load(directory_file.read_text()), str(release / 'mwnet-directory'), previous_release)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    backup = Path('/var/backups/mwnet-release') / stamp
    backup.mkdir(parents=True, mode=0o700)
    shutil.copy2(game_file, backup / 'game-compose.yml')
    shutil.copy2(base_file, backup / 'base-compose.yml')
    shutil.copy2(directory_file, backup / 'directory-compose.yml')
    stopped, changed = False, False
    smoke = []
    volumes = []
    common = ['--network', 'none', '--read-only', '--cap-drop', 'ALL', '--security-opt', 'no-new-privileges',
              '--memory', '2g', '--memory-swap', '2g', '--cpus', '1', '--pids-limit', '128',
              '--mount', f'type=bind,src={release / "smoke-tmp"},dst=/tmp',
              '-e', 'TMPDIR=/tmp', '-e', 'TMP=/tmp', '-e', 'TEMP=/tmp']
    game_mounts = ['--mount', f'type=bind,src={release / "server-package"},dst=/opt/mwnet,readonly',
                   '--mount', f'type=bind,src={release / "entrypoint.py"},dst=/opt/registration/entrypoint.py,readonly',
                   '--mount', f'type=bind,src={content["Source"]},dst=/game-content,readonly']
    report = {'scope': 'NUC release code update; not sixteen-player capacity acceptance', 'outcome': 'running',
              'runtimeSourceCommit': manifest['runtimeSourceCommit'], 'version': manifest['version'],
              'backup': str(backup), 'identityFingerprint': manifest['fingerprint']}
    try:
        for service, previous, destination in [('game', game_old, '/state'), ('directory', directory_old, '/data')]:
            volume = f'mwnet-beta-smoke-{service}-{stamp.lower()}'
            call(['docker', '--context', 'default', 'volume', 'create', volume]); volumes.append(volume)
            name = volume
            args = ['docker', '--context', 'default', 'run', '--detach', '--name', name, *common,
                    '--mount', f'type=volume,src={volume},dst={destination}']
            if service == 'game':
                args += game_mounts + ['-e', 'PUBLIC_ADDRESS=209.25.141.17', '-e', 'PUBLIC_PORT=43133',
                        '-e', 'ANNOUNCE=false', '-e', 'MWNET_CONTENT_DATA_DIR=/game-content',
                        '-e', 'MWNET_CONTENT_FILES=Morrowind.esm,Tribunal.esm,Bloodmoon.esm', previous['Image']]
            else:
                args += ['--mount', f'type=bind,src={release / "mwnet-directory"},dst=/usr/local/bin/mwnet-directory,readonly',
                         previous['Image'], '--origin', 'https://mwnet.playit.plus', '--bind', '127.0.0.1',
                         '--database', '/data/smoke.sqlite3']
            call(args); smoke.append(name)
            wait_ready(name, 'Called "OnServerPostInit"' if service == 'game' else None)
        report['isolatedStartupChecksAccepted'] = True
        call(game + ['stop', 'game']); stopped = True
        call(directory + ['stop', 'directory'])
        for service, previous, destination in [('game', game_old, '/state'), ('directory', directory_old, '/data')]:
            mount = next(m for m in previous['Mounts'] if m['Destination'] == destination)
            if mount['Type'] != 'volume':
                raise ValueError('Expected persistent named volume: ' + service)
            with (backup / (service + '-state.tar.gz')).open('xb') as output:
                call(['docker', '--context', 'default', 'run', '--rm', *common, '--user', '10001:10001',
                      '--mount', f'type=volume,src={mount["Name"]},dst={destination},readonly',
                      '--entrypoint', 'tar', previous['Image'], '-czf', '-', '-C', destination, '.'], output=output)
            call(['tar', '-tzf', str(backup / (service + '-state.tar.gz'))])
        changed = True
        atomic(base_file, yaml.safe_dump(next_base, sort_keys=False))
        atomic(game_file, yaml.safe_dump(next_game, sort_keys=False))
        atomic(directory_file, yaml.safe_dump(next_directory, sort_keys=False))
        call(directory + ['up', '-d', '--no-build', '--pull', 'never', '--no-deps', 'directory'])
        wait_ready('mwnet-directory-directory-1')
        call(game + ['up', '-d', '--no-build', '--pull', 'never', '--no-deps', 'game'])
        wait_ready('mwnet-registration-game-1', 'Called "OnServerPostInit"')
        for name, binary, expected in [('mwnet-registration-game-1', '/opt/mwnet/build/mwnet-server', manifest['serverSha256']),
                                       ('mwnet-directory-directory-1', '/usr/local/bin/mwnet-directory', manifest['directorySha256'])]:
            if call(['docker', '--context', 'default', 'exec', name, 'sha256sum', binary]).split()[0] != expected:
                raise ValueError('Effective runtime binary differs from the accepted candidate')
        managed = call(['docker', '--context', 'default', 'exec', 'mwnet-registration-game-1', 'python3', '-c',
                        'import hashlib,pathlib; root=pathlib.Path("/opt/mwnet/files/mwnet/core-scripts"); n=0; '
                        '\nfor group in ("scripts","lib"):\n for p in (root/group).rglob("*"):\n'
                        '  if p.is_file():\n   q=pathlib.Path("/state/server")/p.relative_to(root); '
                        'assert hashlib.sha256(p.read_bytes()).digest()==hashlib.sha256(q.read_bytes()).digest(); n+=1\n'
                        'print(n)'])
        report['managedServerCodeFilesVerified'] = int(managed.strip())
        output = call(['docker', '--context', 'default', 'run', '--rm', '--network', 'host', '--read-only', '--cap-drop', 'ALL',
                       '--security-opt', 'no-new-privileges', '--memory', '128m', '--cpus', '1', '--pids-limit', '32',
                       '--mount', f'type=bind,src={release / "verify_listing.py"},dst=/verify.py,readonly',
                       '--entrypoint', 'python3', game_old['Image'], '/verify.py', '--origin', 'https://mwnet.playit.plus',
                       '--host', '209.25.141.17', '--port', '43133', '--fingerprint', manifest['fingerprint'],
                       '--expected-version', manifest['version'], '--observe', '65', '--heartbeats', '2', '--deadline', '120'], timeout=200)
        report.update(outcome='pass', signedListingObservations=[json.loads(line) for line in output.splitlines()],
                      effectiveServerSha256=manifest['serverSha256'], effectiveDirectorySha256=manifest['directorySha256'])
        stopped = changed = False
    except Exception:
        if changed:
            call(game + ['stop', 'game']); call(directory + ['stop', 'directory'])
            shutil.copy2(backup / 'game-compose.yml', game_file)
            shutil.copy2(backup / 'base-compose.yml', base_file)
            shutil.copy2(backup / 'directory-compose.yml', directory_file)
            for service, previous, destination in [('game', game_old, '/state'), ('directory', directory_old, '/data')]:
                mount = next(m for m in previous['Mounts'] if m['Destination'] == destination)
                with (backup / (service + '-state.tar.gz')).open('rb') as source:
                    args = ['docker', '--context', 'default', 'run', '--rm', '-i', *common, '--user', '10001:10001',
                            '--mount', f'type=volume,src={mount["Name"]},dst={destination}',
                            '--entrypoint', 'bash', previous['Image'], '-c',
                            f'find {destination} -mindepth 1 -delete && tar -xzf - -C {destination}']
                    subprocess.run(args, stdin=source, check=True, timeout=120)
        if stopped:
            call(directory + ['up', '-d', '--no-build', '--pull', 'never', '--no-deps', 'directory'])
            call(game + ['up', '-d', '--no-build', '--pull', 'never', '--no-deps', 'game'])
        raise
    finally:
        for name in smoke:
            subprocess.run(['docker', '--context', 'default', 'rm', '--force', name], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for volume in volumes:
            subprocess.run(['docker', '--context', 'default', 'volume', 'rm', volume], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    report['completedUtc'] = datetime.now(timezone.utc).isoformat()
    with receipt.open('x') as stream:
        stream.write(json.dumps(report, indent=2) + '\n')
    os.chown(receipt, bundle.stat().st_uid, bundle.stat().st_gid)
    print('PASS: matching release game/scripts and directory code running; identity and signed heartbeats verified.')


if __name__ == '__main__':
    try:
        run(Path(__file__).resolve().parent)
    except Exception as error:
        message = 'Host configuration could not be parsed' if isinstance(error, yaml.YAMLError) else str(error)
        print('Release update failed: ' + message, file=sys.stderr)
        raise SystemExit(1)
