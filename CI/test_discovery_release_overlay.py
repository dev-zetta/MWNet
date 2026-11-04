#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('release_overlay', ROOT / 'docker/discovery/release_overlay.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class ReleaseOverlayTests(unittest.TestCase):
    def test_game_upgrade_preserves_data_settings_and_limits(self):
        data = {'type': 'bind', 'source': '${MWNET_CONTENT_HOST_DIR}', 'target': '/game-content', 'read_only': True}
        old = {'services': {'game': {'volumes': [data, 'game-state:/state',
                MODULE.bind('/srv/mwnet/server-patches/old/mwnet-server', '/opt/mwnet/build/mwnet-server')],
                'environment': {'MWNET_CONTENT_FILES': 'Morrowind.esm,Tribunal.esm,Bloodmoon.esm'},
                'mem_limit': '2g', 'cpus': 2}}}
        new = MODULE.game_overlay(old, '/srv/mwnet/releases/candidate')
        service = new['services']['game']
        self.assertEqual(service['volumes'][:2], [data, 'game-state:/state'])
        self.assertEqual(service['environment'], old['services']['game']['environment'])
        self.assertEqual((service['mem_limit'], service['cpus']), ('2g', 2))
        self.assertNotIn('/opt/mwnet/build/mwnet-server', [MODULE.target(v) for v in service['volumes']])
        self.assertEqual(len(old['services']['game']['volumes']), 3)
        self.assertEqual(MODULE.game_overlay(new, '/srv/mwnet/releases/candidate'), new)

    def test_unknown_binary_override_is_not_discarded(self):
        old = {'services': {'game': {'volumes': [MODULE.bind('/operator/custom-server', '/opt/mwnet/build/mwnet-server')]}}}
        with self.assertRaisesRegex(ValueError, 'Unrecognized'):
            MODULE.game_overlay(old, '/srv/mwnet/releases/candidate')

    def test_directory_upgrade_preserves_tls_proxy_and_database(self):
        old = {'services': {'directory': {'volumes': ['directory-data:/data'], 'mem_limit': '512m'},
                            'https': {'image': 'pinned-caddy', 'volumes': ['caddy-data:/data']}}}
        new = MODULE.directory_overlay(old, '/srv/mwnet/releases/candidate/mwnet-directory')
        self.assertEqual(new['services']['https'], old['services']['https'])
        self.assertEqual(new['services']['directory']['volumes'][0], 'directory-data:/data')
        self.assertEqual(new['services']['directory']['mem_limit'], '512m')
        self.assertEqual(MODULE.directory_overlay(new, '/srv/mwnet/releases/candidate/mwnet-directory'), new)

    def test_relative_paths_and_unknown_code_mounts_are_rejected(self):
        with self.assertRaises(ValueError):
            MODULE.game_overlay({'services': {'game': {}}}, '../release')
        with self.assertRaises(ValueError):
            MODULE.directory_overlay({'services': {'directory': {}}}, 'relative')
        with self.assertRaises(ValueError):
            MODULE.game_overlay({'services': {'game': {'volumes': [MODULE.bind('/custom/package', '/opt/mwnet')]}}}, '/srv/mwnet/release')

    def test_disk_scratch_replaces_only_the_tmp_ram_mount(self):
        base = {'services': {'game': {'tmpfs': ['/tmp:rw,size=16m', '/run:rw,size=4m']}}}
        new = MODULE.game_base(base)
        self.assertEqual(new['services']['game']['tmpfs'], ['/run:rw,size=4m'])
        overlay = MODULE.game_overlay({'services': {'game': {}}}, '/srv/mwnet/release')
        temporary = next(v for v in overlay['services']['game']['volumes'] if v['target'] == '/tmp')
        self.assertFalse(temporary['read_only'])
        self.assertEqual(temporary['source'], '/srv/mwnet/release/tmp')
        self.assertEqual(len(base['services']['game']['tmpfs']), 2)


if __name__ == '__main__':
    unittest.main()
