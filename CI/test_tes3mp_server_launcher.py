#!/usr/bin/env python3
"""Check fresh server setup, state preservation, and explicit profile imports."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class ServerLauncherTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='tes3mp-launcher-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        source = Path(__file__).resolve().parents[1]
        shutil.copy2(source / 'run-tes3mp-server.sh', self.root)
        core = self.root / 'files/tes3mp/core-scripts'
        for name in ('scripts', 'lib', 'data'):
            (core / name).mkdir(parents=True)
        (core / 'scripts/serverCore.lua').write_text('-- fixture\n')
        (core / 'data/moderation.json').write_text('{}\n')
        (core / 'data/local-state.json').write_text('{"fixture": true}\n')
        shutil.copytree(source / 'files/tes3mp/core-scripts/defaults', core / 'defaults')
        (self.root / 'build').mkdir()
        binary = self.root / 'build/tes3mp-server'
        binary.write_text('#!/bin/sh\nexit 99\n')
        binary.chmod(0o755)
        self.state = self.root / 'state'
        self.environment = os.environ.copy()
        self.environment.pop('TES3MP_SERVER_PROFILE', None)
        self.environment.update(TES3MP_TEST_ROOT=str(self.state),
                                TES3MP_BUILD_DIR=str(self.root / 'build'))

    def prepare(self):
        subprocess.run(['bash', str(self.root / 'run-tes3mp-server.sh'), '--prepare-only'],
                       env=self.environment, check=True, capture_output=True, timeout=10)

    def test_fresh_setup_excludes_implicit_local_state(self):
        self.prepare()
        data = self.state / 'server/data'
        self.assertEqual(json.loads((data / 'banlist.json').read_text()),
                         {'playerNames': [], 'ipAddresses': []})
        self.assertEqual(len(json.loads((data / 'requiredDataFiles.json').read_text())), 3)
        self.assertFalse((data / 'local-state.json').exists())
        for name in ('cell', 'custom', 'map', 'player', 'recordstore', 'world'):
            self.assertTrue((data / name).is_dir())

    def test_existing_settings_are_preserved(self):
        self.prepare()
        data = self.state / 'server/data'
        expected = {'banlist.json': '{"playerNames":["fixture"]}\n',
                    'requiredDataFiles.json': '[]\n', 'moderation.json': '{"fixture":1}\n'}
        for name, content in expected.items():
            (data / name).write_text(content)
        self.prepare()
        for name, content in expected.items():
            self.assertEqual((data / name).read_text(), content)

    def test_explicit_profile_is_imported_once(self):
        profile = self.root / 'profile/data'
        profile.mkdir(parents=True)
        (profile / 'requiredDataFiles.json').write_text('[]\n')
        (profile / 'profile-state.json').write_text('{"fixture":1}\n')
        self.environment['TES3MP_SERVER_PROFILE'] = str(profile.parent)
        self.prepare()
        (profile / 'profile-state.json').write_text('{"fixture":2}\n')
        self.prepare()
        data = self.state / 'server/data'
        self.assertEqual((data / 'requiredDataFiles.json').read_text(), '[]\n')
        self.assertEqual((data / 'profile-state.json').read_text(), '{"fixture":1}\n')
        self.assertTrue((data / 'banlist.json').exists())


if __name__ == '__main__':
    unittest.main()
