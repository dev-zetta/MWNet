#!/usr/bin/env python3
"""Validate tunnel exposure and credentials with Compose; no daemon or agent runs."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

import yaml


ROOT = Path(__file__).resolve().parents[1]
DISCOVERY = ROOT / 'docker/discovery'
SPEC = importlib.util.spec_from_file_location('discovery_iso', DISCOVERY / 'appliance/build-iso.py')
BUILDER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILDER)


def has_compose():
    if not shutil.which('docker'):
        return False
    return subprocess.run(['docker', 'compose', 'version'], capture_output=True,
                          timeout=10).returncode == 0


@unittest.skipUnless(has_compose(), 'Docker Compose 2.30+ is required; daemon is not needed')
class ComposeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name)
        self.credentials = self.folder / 'agent.env'
        # Deliberately fake; includes characters Compose must not interpolate.
        self.secret = 'offline-test$MWNET_UNSET_${MWNET_UNSET}#literal="quotes"'
        self.credentials.write_text(f'SECRET_KEY={self.secret}\n')
        self.environment = dict(os.environ, DIRECTORY_HOST='directory.example.invalid',
                                PLAYIT_ENV_FILE=str(self.credentials))
        self.environment.pop('MWNET_UNSET', None)

    def compose(self, *files, success=True):
        command = ['docker', 'compose', '-p', 'mwnet-config-test']
        for path in files:
            command.extend(['-f', str(path)])
        result = subprocess.run(command + ['config', '--format', 'json'],
                                env=self.environment, text=True, capture_output=True, timeout=20)
        if not success:
            self.assertNotEqual(result.returncode, 0)
            return result
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def assert_private_directory(self, config):
        services = config['services']
        self.assertEqual(set(services), {'directory', 'https'})
        self.assertFalse(services['directory'].get('ports'))
        ports = services['https']['ports']
        self.assertEqual(len(ports), 2, 'Public listeners survived Compose merging')
        self.assertEqual({(p['host_ip'], str(p['published']), p['target'], p['protocol'])
                          for p in ports},
                         {('127.0.0.1', '80', 80, 'tcp'), ('127.0.0.1', '443', 443, 'tcp')})

    def test_overlay_replaces_public_bindings_for_checkout(self):
        config = self.compose(DISCOVERY / 'compose.yml', DISCOVERY / 'compose.playit.yml')
        self.assert_private_directory(config)

    def test_overlay_preserves_appliance_images_and_volumes(self):
        base = BUILDER.runtime_compose('mwnet-directory:offline-test')
        base['services']['https']['image'] = 'mwnet-caddy:offline-test'
        path = self.folder / 'compose.yml'
        path.write_text(yaml.safe_dump(base))
        config = self.compose(path, DISCOVERY / 'compose.playit.yml')
        self.assert_private_directory(config)
        for service, image in (('directory', 'mwnet-directory:offline-test'),
                               ('https', 'mwnet-caddy:offline-test')):
            self.assertEqual(config['services'][service]['image'], image)
            self.assertEqual(config['services'][service]['pull_policy'], 'never')
            self.assertNotIn('build', config['services'][service])
        self.assertEqual(set(config['volumes']), {'directory-data', 'caddy-data', 'caddy-config'})

    def test_agent_preserves_literal_secret_and_has_no_inbound_ports(self):
        config = self.compose(DISCOVERY / 'compose.playit-agent.yml')
        self.assertEqual(set(config['services']), {'playit'})
        service = config['services']['playit']
        # Canonical Compose output escapes dollars for safe reloading as input.
        self.assertEqual(service['environment']['SECRET_KEY'], self.secret.replace('$', '$$'))
        self.assertEqual(service['network_mode'], 'host')
        self.assertFalse(service.get('ports'))
        self.assertFalse(service.get('volumes'), 'Agent must not mount the Docker socket')
        self.assertFalse(service.get('privileged'))
        self.assertNotIn('depends_on', service)
        self.assertEqual(service['restart'], 'unless-stopped')
        self.assertRegex(service['image'], r'^ghcr\.io/playit-cloud/playit-agent:1\.0@sha256:[0-9a-f]{64}$')
        self.assertGreater(int(service['mem_limit']), 0)
        self.assertGreater(float(service['cpus']), 0)
        self.assertGreater(service['pids_limit'], 0)
        self.assertEqual(service['logging']['options'], {'max-size': '10m', 'max-file': '3'})

    def test_missing_credential_file_fails_closed(self):
        self.credentials.unlink()
        result = self.compose(DISCOVERY / 'compose.playit-agent.yml', success=False)
        self.assertIn('agent.env', result.stderr)
        self.assertNotIn(self.secret, result.stderr)

    def test_base_deployment_does_not_start_agent(self):
        config = self.compose(DISCOVERY / 'compose.yml')
        self.assertEqual(set(config['services']), {'directory', 'https'})
        self.assertTrue(all(p.get('host_ip', '0.0.0.0') == '0.0.0.0'
                            for p in config['services']['https']['ports']))


class PackagingTests(unittest.TestCase):
    def test_appliance_copies_only_templates_not_local_credentials(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            source, destination = folder / 'source', folder / 'payload'
            source.mkdir()
            destination.mkdir()
            for name in BUILDER.TUNNEL_FILES:
                (source / name).write_bytes((DISCOVERY / name).read_bytes())
            for name in ('agent.env', '.env', 'playit.secret'):
                (source / name).write_text('DO_NOT_PACKAGE')
            with mock.patch.object(BUILDER, 'HERE', source / 'appliance'):
                BUILDER.copy_tunnel_files(destination)
            self.assertEqual({p.name for p in destination.iterdir()}, set(BUILDER.TUNNEL_FILES))
            self.assertFalse(any('DO_NOT_PACKAGE' in p.read_text() for p in destination.iterdir()))

    def test_packaging_rejects_template_symlink_to_credentials(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            source, destination = folder / 'source', folder / 'payload'
            source.mkdir()
            destination.mkdir()
            secret = folder / 'agent.env'
            secret.write_text('DO_NOT_PACKAGE')
            (source / BUILDER.TUNNEL_FILES[0]).symlink_to(secret)
            with mock.patch.object(BUILDER, 'HERE', source / 'appliance'):
                with self.assertRaisesRegex(ValueError, 'regular file'):
                    BUILDER.copy_tunnel_files(destination)
            self.assertFalse(list(destination.iterdir()))

    @unittest.skipUnless(shutil.which('git'), 'git is required')
    def test_git_ignores_host_credentials_but_keeps_examples(self):
        for name in ('agent.env', '.env', '.env.production', 'playit.secret', 'playit-secret'):
            result = subprocess.run(['git', '-C', str(ROOT), 'check-ignore', '--no-index',
                                     str(DISCOVERY / name)], capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, name)
        for name in ('playit.env.example', '.env.example', 'compose.playit-agent.yml'):
            result = subprocess.run(['git', '-C', str(ROOT), 'check-ignore', '--no-index',
                                     str(DISCOVERY / name)], capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 1, name)


if __name__ == '__main__':
    unittest.main()
