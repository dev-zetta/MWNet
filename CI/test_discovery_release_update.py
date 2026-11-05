#!/usr/bin/env python3
import importlib.util
import hashlib
import io
import json
import os
from pathlib import Path
import sys
import tarfile
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

import yaml

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'docker/discovery'))
SPEC = importlib.util.spec_from_file_location('release_apply', ROOT / 'docker/discovery/apply_release_update.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def manifest():
    return {'runtimeSourceCommit': 'a' * 40, 'version': '1.0.0-beta.1',
            'fingerprint': 'ed25519/' + 'A' * 43, 'previousGameBinarySha256': 'b' * 64,
            'serverSha256': 'c' * 64, 'directorySha256': 'd' * 64,
            'acceptedGameImageIds': ['sha256:' + 'e' * 64],
            'inputs': {'server-package/build/mwnet-server': 'c' * 64, 'mwnet-directory': 'd' * 64}}


class ReleaseUpdateTests(unittest.TestCase):
    def test_selected_release_matches_binary_inventory(self):
        MODULE.validate_manifest(manifest())
        for field, value in [('runtimeSourceCommit', '../../../etc/new'), ('version', 'beta\nother'),
                             ('serverSha256', 'wrong'), ('acceptedGameImageIds', ['unaccepted'])]:
            with self.subTest(field=field), self.assertRaises(ValueError):
                MODULE.validate_manifest(manifest() | {field: value})

    def test_escaping_or_inconsistent_input_inventory_is_rejected(self):
        for inputs in ({'../outside': 'f' * 64}, {'/etc/file': 'f' * 64},
                       {'server-package/build/mwnet-server': 'f' * 64, 'mwnet-directory': 'd' * 64}):
            with self.subTest(inputs=inputs), self.assertRaises(ValueError):
                MODULE.validate_manifest(manifest() | {'inputs': inputs})

    def test_subprocess_output_is_captured_and_failure_rejected(self):
        self.assertEqual(MODULE.call(['printf', 'verified']), 'verified')
        with self.assertRaises(RuntimeError):
            MODULE.call(['false'])

    def test_atomic_configuration_replacement_preserves_owner(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'compose.yml'; path.write_text('old')
            owner = path.stat().st_uid
            MODULE.atomic(path, 'new')
            self.assertEqual(path.read_text(), 'new')
            self.assertEqual(path.stat().st_uid, owner)
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)

    def test_failed_post_swap_startup_restores_configs_and_both_state_backups(self):
        self.exercise_release(injected_failure=True)

    def test_success_requires_runtime_hashes_and_signed_listing_observation(self):
        self.exercise_release(injected_failure=False)

    def test_verified_previous_release_upgrade_passes(self):
        self.exercise_release(injected_failure=False, previous_release=True)

    def test_previous_release_upgrade_rolls_back_both_configs_and_state(self):
        self.exercise_release(injected_failure=True, previous_release=True)

    def test_changed_previous_directory_is_rejected_before_stopping(self):
        self.exercise_release(injected_failure=False, previous_release=True, wrong_directory=True)

    def test_previous_release_requires_both_source_and_directory_digest(self):
        for fields in [{'previousRuntimeSourceCommit': 'f' * 40}, {'previousDirectorySha256': 'f' * 64},
                       {'previousRuntimeSourceCommit': '../escape', 'previousDirectorySha256': 'f' * 64}]:
            with self.assertRaises(ValueError):
                MODULE.validate_manifest(manifest() | fields)

    def exercise_release(self, injected_failure, previous_release=False, wrong_directory=False):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); bundle = root / 'bundle'; bundle.mkdir()
            original_path = Path
            def mapped(value):
                value = original_path(value)
                if value.is_absolute() and not value.is_relative_to(root):
                    return root / value.relative_to('/')
                return value
            files = {}
            settings = {
                '/etc/mwnet-gameplay/compose.content.yml': {'services': {'game': {'volumes': [
                    {'type': 'bind', 'source': '/srv/mwnet/server-patches/old/mwnet-server',
                     'target': '/opt/mwnet/build/mwnet-server', 'read_only': True}], 'mem_limit': '2g'}}},
                '/home/gmax/mwnet-registration/compose.yml': {'services': {'game': {'tmpfs': ['/tmp:rw,size=16m']}}},
                '/opt/mwnet/discovery-appliance/compose.yml': {'services': {'directory': {'volumes': ['directory-data:/data']}}},
            }
            if previous_release:
                previous = '/srv/mwnet/releases/' + 'f' * 40
                settings['/etc/mwnet-gameplay/compose.content.yml'] = MODULE.game_overlay(
                    {'services': {'game': {'volumes': ['game-state:/state'], 'mem_limit': '2g'}}}, previous)
                settings['/opt/mwnet/discovery-appliance/compose.yml'] = MODULE.directory_overlay(
                    {'services': {'directory': {'volumes': ['directory-data:/data']}}}, previous + '/mwnet-directory')
            for name, data in settings.items():
                path = mapped(name); path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(yaml.safe_dump(data)); files[name] = path.read_bytes()
            mapped('/run/lock').mkdir(parents=True)
            mapped('/srv/mwnet').mkdir(parents=True)
            for name, data in [('server-package/build/mwnet-server', b'new-server'),
                               ('entrypoint.py', b'entry'), ('verify_listing.py', b'verifier'),
                               ('mwnet-directory', b'new-directory')]:
                path = bundle / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
            selected = manifest()
            if previous_release:
                selected.update(version='1.0.0', previousRuntimeSourceCommit='f' * 40, previousDirectorySha256='f' * 64)
            selected['inputs'] = {str(p.relative_to(bundle)): hashlib.sha256(p.read_bytes()).hexdigest()
                                  for p in bundle.rglob('*') if p.is_file()}
            selected['serverSha256'] = selected['inputs']['server-package/build/mwnet-server']
            selected['directorySha256'] = selected['inputs']['mwnet-directory']
            (bundle / 'deployment.json').write_text(json.dumps(selected))
            states = {'game': b'account-and-world', 'directory': b'listing-database'}
            originals = dict(states); events = []
            image = selected['acceptedGameImageIds'][0]
            def inspected(name):
                game = name == 'mwnet-registration-game-1'
                return {'Id': 'game-id' if game else 'directory-id', 'Image': image,
                        'Mounts': [{'Destination': '/state' if game else '/data', 'Type': 'volume',
                                    'Name': 'game-volume' if game else 'directory-volume'},
                                   {'Destination': '/game-content', 'Source': str(root / 'content'), 'RW': False}]}
            def command(args, timeout=120, output=None):
                if args[0] == 'stat': return 'btrfs\n'
                if args[:4] == ['docker', '--context', 'default', 'exec']:
                    if 'python3' in args:
                        return '42\n'
                    if selected['runtimeSourceCommit'] in mapped('/etc/mwnet-gameplay/compose.content.yml').read_text():
                        key = 'serverSha256' if args[-1].endswith('mwnet-server') else 'directorySha256'
                        return selected[key] + '  binary\n'
                    if args[-1].endswith('mwnet-directory'):
                        return ('0' * 64 if wrong_directory else selected['previousDirectorySha256']) + '  binary\n'
                    return selected['previousGameBinarySha256'] + '  binary\n'
                if '/verify.py' in args:
                    self.assertEqual(args[args.index('--expected-version') + 1], selected['version'])
                    return json.dumps({'signedListingObservation': 'accepted-by-verifier'}) + '\n'
                if output is not None:
                    service = 'game' if any('src=game-volume,' in str(a) for a in args) else 'directory'
                    events.append(('backup', service))
                    with tarfile.open(fileobj=output, mode='w:gz') as archive:
                        member = tarfile.TarInfo('state'); member.size = len(states[service])
                        archive.addfile(member, io.BytesIO(states[service]))
                    return ''
                if args[0] == 'tar':
                    with tarfile.open(args[-1]): pass
                if 'compose' in args:
                    if 'mwnet-directory' in args:
                        self.assertIn('/etc/mwnet-directory.env', args)
                    action, service = ('stop' if 'stop' in args else 'up'), args[-1]
                    events.append((action, service))
                    if action == 'up' and selected['runtimeSourceCommit'] in mapped('/opt/mwnet/discovery-appliance/compose.yml').read_text():
                        states['directory'] = b'changed-after-upgrade'
                return ''
            def process(args, **kwargs):
                if '-i' in args:
                    service = 'game' if any('src=game-volume,' in str(a) for a in args) else 'directory'
                    with tarfile.open(fileobj=kwargs['stdin'], mode='r:gz') as archive:
                        states[service] = archive.extractfile('state').read()
                    events.append(('restore', service))
                return SimpleNamespace(returncode=0)
            def ready(name, marker=None):
                if injected_failure and name == 'mwnet-directory-directory-1':
                    raise RuntimeError('injected post-swap failure')
            old_umask = os.umask(0o077); self.addCleanup(os.umask, old_umask)
            with mock.patch.object(MODULE, 'Path', side_effect=mapped), \
                 mock.patch.object(MODULE.os, 'geteuid', return_value=0), \
                 mock.patch.object(MODULE.os, 'chown'), \
                 mock.patch.object(MODULE.shutil, 'disk_usage', return_value=SimpleNamespace(free=10 * 1024 ** 3)), \
                 mock.patch.object(MODULE, 'inspect', side_effect=inspected), \
                 mock.patch.object(MODULE, 'call', side_effect=command), \
                 mock.patch.object(MODULE, 'wait_ready', side_effect=ready), \
                 mock.patch.object(MODULE.subprocess, 'run', side_effect=process):
                if wrong_directory:
                    with self.assertRaisesRegex(ValueError, 'running directory binary changed'):
                        MODULE.run(bundle)
                elif injected_failure:
                    with self.assertRaisesRegex(RuntimeError, 'injected post-swap failure'):
                        MODULE.run(bundle)
                else:
                    MODULE.run(bundle)
            if wrong_directory:
                self.assertEqual(events, [])
                self.assertEqual(states, originals)
                self.assertTrue(all(mapped(name).read_bytes() == data for name, data in files.items()))
                return
            if not injected_failure:
                report = json.loads((bundle / 'deployment-result.json').read_text())
                self.assertEqual(report['outcome'], 'pass')
                self.assertEqual(report['effectiveServerSha256'], selected['serverSha256'])
                self.assertEqual(report['effectiveDirectorySha256'], selected['directorySha256'])
                self.assertEqual(report['managedServerCodeFilesVerified'], 42)
                self.assertEqual(len(report['signedListingObservations']), 1)
                self.assertFalse(any(event[0] == 'restore' for event in events))
                return
            self.assertEqual(states, originals)
            self.assertTrue(all(mapped(name).read_bytes() == data for name, data in files.items()))
            self.assertLess(events.index(('backup', 'game')), events.index(('up', 'directory')))
            self.assertLess(events.index(('restore', 'game')), len(events) - 1)
            self.assertIn(('restore', 'directory'), events)
            self.assertEqual(events[-2:], [('up', 'directory'), ('up', 'game')])
            self.assertFalse((bundle / 'deployment-result.json').exists())


if __name__ == '__main__':
    unittest.main()
