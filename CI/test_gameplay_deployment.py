#!/usr/bin/env python3
"""Exercise the real root helper in a disposable chroot with a controlled Docker CLI.

Set MWNET_GAMEPLAY_TEST_ROOTFS to an extracted Ubuntu runtime below build/ and run
inside a user namespace with a root mapping and subordinate UID/GID mappings.
The Docker responses are fixtures; native content startup is tested separately.
"""
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = os.environ.get('MWNET_GAMEPLAY_TEST_ROOTFS')
FAKE_DOCKER = r'''#!/usr/bin/python3
import io,json,pathlib,sys,tarfile
a=sys.argv[1:]
m=pathlib.Path('/mock')
scenario=(m/'scenario').read_text()
with (m/'commands.jsonl').open('a') as stream: stream.write(json.dumps(a)+'\n')
if a[:2]==['image','inspect']:
    print(json.dumps({'id':'sha256:'+('e' if scenario=='wrong-image' else 'c')*64,
                      'os':'linux','architecture':'amd64','revision':'d'*40}))
elif a[0]=='compose':
    if 'config' in a:
        if '--images' in a: print('mwnet-registration:test')
    elif 'logs' in a:
        print('Server identity fingerprint: ed25519/test')
        print('Loaded 1877 canonical spell and enchanted-item definitions and 3437 actor templates')
        print('Installed 3424 canonical NPC recovery anchors')
        print('Called "OnServerPostInit"')
    elif 'ps' in a: print('fixture-container')
    elif 'up' in a:
        if scenario in ('start-fail','existing-fail') and not (m/'failed-once').exists():
            (m/'failed-once').touch(); sys.exit(27)
    elif 'stop' not in a: sys.exit('Unexpected compose command')
elif a[0]=='run':
    if '--entrypoint' in a and a[a.index('--entrypoint')+1]=='tar':
        with tarfile.open(fileobj=sys.stdout.buffer, mode='w:gz') as archive:
            payload=b'fixture identity, never a production key'
            info=tarfile.TarInfo('server-identity.key'); info.size=len(payload); info.mode=0o600
            archive.addfile(info,io.BytesIO(payload))
    elif '/opt/registration/verify_listing.py' in a:
        if scenario=='verify-fail': sys.exit(28)
        assert '--observe' in a and a[a.index('--observe')+1]=='65'
    else: sys.exit('Unexpected docker run')
elif a[0]=='inspect':
    if a[2].startswith('Running='): print('Running=true OOMKilled=false MemoryLimit=2147483648 NanoCpus=2000000000')
    else: print('true false 0 2147483648 2000000000')
elif a[0]=='cp': pathlib.Path(a[-1]).write_text('fixture-only-access-token\n')
elif a[0]=='stats': print('fixture-container Memory=64MiB / 2GiB')
else: sys.exit('Unexpected Docker command: '+str(a))
'''


@unittest.skipUnless(RUNTIME and os.geteuid() == 0, 'Requires disposable Ubuntu rootfs and a root user namespace')
class DeploymentTests(unittest.TestCase):
    def setUp(self):
        self.fs = Path(RUNTIME).resolve()
        if not self.fs.is_relative_to(ROOT / 'build') or self.fs == ROOT / 'build':
            self.fail('Refusing a rootfs outside the workspace build directory')
        for name in ('home/gmax', 'etc/mwnet-gameplay', 'srv/mwnet', 'var/backups/mwnet-gameplay', 'mock', 'mockbin'):
            p = self.fs / name
            if p.exists():
                shutil.rmtree(p)
        for name in ('run/lock', 'dev', 'home/gmax/mwnet-gameplay/content', 'home/gmax/mwnet-registration', 'mockbin', 'mock'):
            (self.fs / name).mkdir(parents=True, exist_ok=True)
        (self.fs / 'dev/null').touch()
        self.bundle = self.fs / 'home/gmax/mwnet-gameplay'
        for name in ('enable-gameplay.sh', 'verify_image.py', 'compose.content.yml'):
            shutil.copy2(ROOT / 'docker/discovery/gameplay' / name, self.bundle / name)
        files = {}
        for name in ('Morrowind.esm', 'Tribunal.esm', 'Bloodmoon.esm'):
            data = b'TES3fixture-' + name.encode()
            (self.bundle / 'content' / name).write_bytes(data)
            files[name] = {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
        manifest = {'files': files, 'fingerprint': 'ed25519/test', 'runtimeSourceCommit': 'd' * 40,
                    'imageDigests': dict(zip(('config', 'manifest', 'index'), ('sha256:' + c * 64 for c in 'abc')))}
        (self.bundle / 'content.json').write_text(json.dumps(manifest))
        sums = []
        for p in sorted(self.bundle.rglob('*')):
            if p.is_file():
                sums.append(hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + str(p.relative_to(self.bundle)) + '\n')
        (self.bundle / 'SHA256SUMS').write_text(''.join(sums))
        for p in [self.bundle, *self.bundle.rglob('*')]:
            os.chown(p, 1000, 1000)
        self.bundle.chmod(0o700)
        registration = self.fs / 'home/gmax/mwnet-registration'
        (registration / '.env').write_text('PUBLIC_ADDRESS=209.25.141.17\nPUBLIC_PORT=43133\n')
        (registration / 'compose.yml').write_text('services: {}\n')
        (self.fs / 'mockbin/docker').write_text(FAKE_DOCKER)
        (self.fs / 'mockbin/docker').chmod(0o755)

    def invoke(self, scenario):
        (self.fs / 'mock/scenario').write_text(scenario)
        def enter():
            os.chroot(self.fs)
            os.chdir('/')
        result = subprocess.run(['/bin/bash', '/home/gmax/mwnet-gameplay/enable-gameplay.sh'],
                                preexec_fn=enter, env={'PATH': '/mockbin:/usr/bin:/bin', 'HOME': '/root'},
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20)
        self.assertNotIn('fixture-only-access-token', result.stdout)
        commands = [json.loads(line) for line in (self.fs / 'mock/commands.jsonl').read_text().splitlines()]
        self.assertFalse(any(c[:2] in (['volume', 'rm'], ['system', 'prune']) for c in commands))
        return result, commands

    def test_containerd_id_upgrade_and_private_backup(self):
        result, commands = self.invoke('success')
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn('Accepted game image: index=', result.stdout)
        self.assertIn('PASS: canonical content loaded', result.stdout)
        archive = next((self.fs / 'var/backups/mwnet-gameplay').glob('*/game-state.tar.gz'))
        self.assertEqual(archive.stat().st_mode & 0o777, 0o600)
        with tarfile.open(archive) as stream:
            self.assertIn('server-identity.key', stream.getnames())
        credential = self.fs / 'home/gmax/.mwnet-gameplay-access-password'
        self.assertEqual(credential.stat().st_uid, 1000)
        self.assertEqual(credential.stat().st_mode & 0o777, 0o600)
        self.assertTrue((self.fs / 'etc/mwnet-gameplay/compose.content.yml').is_file())
        stop = next(i for i, c in enumerate(commands) if c[0] == 'compose' and 'stop' in c)
        backup = next(i for i, c in enumerate(commands) if c[0] == 'run' and 'tar' in c)
        start = next(i for i, c in enumerate(commands) if c[0] == 'compose' and 'up' in c)
        self.assertLess(stop, backup)
        self.assertLess(backup, start)

    def test_wrong_image_fails_before_stopping_game(self):
        result, commands = self.invoke('wrong-image')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Unaccepted game image ID', result.stdout)
        self.assertFalse(any(c[0] == 'compose' and 'stop' in c for c in commands))
        self.assertFalse((self.fs / 'var/backups/mwnet-gameplay').exists())

    def test_start_or_discovery_failure_rolls_back(self):
        for scenario in ('start-fail', 'verify-fail'):
            with self.subTest(scenario=scenario):
                self.setUp()
                result, commands = self.invoke(scenario)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('restoring the previous container configuration', result.stdout)
                self.assertFalse((self.fs / 'etc/mwnet-gameplay/compose.content.yml').exists())
                self.assertFalse((self.fs / 'home/gmax/.mwnet-gameplay-access-password').exists())
                self.assertTrue(any((self.fs / 'var/backups/mwnet-gameplay').glob('*/game-state.tar.gz')))
                starts = [c for c in commands if c[0] == 'compose' and 'up' in c]
                self.assertEqual(len(starts), 2)
                self.assertNotIn('/etc/mwnet-gameplay/compose.content.yml', starts[-1])

    def test_existing_configuration_is_restored(self):
        settings = self.fs / 'etc/mwnet-gameplay'
        settings.mkdir(parents=True)
        (settings / 'compose.content.yml').write_text('previous overlay\n')
        (settings / 'content.env').write_text('previous content location\n')
        result, _ = self.invoke('existing-fail')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual((settings / 'compose.content.yml').read_text(), 'previous overlay\n')
        self.assertEqual((settings / 'content.env').read_text(), 'previous content location\n')


if __name__ == '__main__':
    unittest.main()
