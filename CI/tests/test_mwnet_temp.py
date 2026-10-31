#!/usr/bin/env python3
"""Disk scratch selection, permission boundaries and subprocess inheritance."""
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'CI'))
import mwnet_temp as MODULE


class DiskTemporaryStorageTests(unittest.TestCase):
    def setUp(self):
        self.workspace = tempfile.TemporaryDirectory(dir=MODULE.prepare_temp_root())
        self.addCleanup(self.workspace.cleanup)
        self.root = Path(self.workspace.name)

    def test_default_ignores_system_temp_variables(self):
        with mock.patch.object(MODULE, 'ROOT', self.root), mock.patch.dict(
                os.environ, {'MWNET_TMPDIR': '', 'TMPDIR': '/tmp', 'TMP': '/tmp', 'TEMP': '/tmp'}):
            chosen = MODULE.prepare_temp_root()
        self.assertEqual(chosen, self.root / 'tmp')
        if os.name == 'posix':
            self.assertEqual(stat.S_IMODE(chosen.stat().st_mode), 0o700)

    def test_explicit_directory_precedes_environment(self):
        with mock.patch.dict(os.environ, {'MWNET_TMPDIR': '/tmp/unused'}):
            chosen = MODULE.prepare_temp_root(self.root / 'explicit')
        self.assertEqual(chosen, self.root / 'explicit')

    def test_configure_updates_python_cached_temp_and_all_child_variables(self):
        original_cache = tempfile.tempdir
        self.addCleanup(setattr, tempfile, 'tempdir', original_cache)
        with mock.patch.dict(os.environ):
            tempfile.tempdir = '/tmp'
            chosen = MODULE.configure_temp_environment(self.root / 'configured')
            self.assertEqual(tempfile.gettempdir(), str(chosen))
            with tempfile.TemporaryDirectory() as generated:
                self.assertEqual(Path(generated).parent, chosen)
            self.assertEqual({os.environ[key] for key in ('TMPDIR', 'TMP', 'TEMP')}, {str(chosen)})

    def test_launcher_preserves_arguments_exit_status_and_nested_environment(self):
        selected = self.root / 'scratch with spaces'
        code = ('import json,os,sys,tempfile; '
                'print(json.dumps({"variables":{k:os.environ.get(k) for k in '
                '("TMPDIR","TMP","TEMP","MWNET_TMPDIR")},'
                '"pythonTemp":tempfile.gettempdir(),"args":sys.argv[1:]})); sys.exit(17)')
        result = subprocess.run(
            ['bash', str(ROOT / 'CI/with_disk_tmp.sh'), sys.executable, '-c', code, 'argument with spaces'],
            env=dict(os.environ, MWNET_TMPDIR=str(selected), TMPDIR='/tmp'),
            capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 17, result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(set(report['variables'].values()), {str(selected)})
        self.assertEqual(report['pythonTemp'], str(selected))
        self.assertEqual(report['args'], ['argument with spaces'])

    def test_memory_filesystem_rejected_before_creation(self):
        for filesystem in ('tmpfs', 'ramfs', 'devtmpfs'):
            with self.subTest(filesystem=filesystem), mock.patch.object(
                    MODULE, '_filesystem_type', return_value=filesystem):
                target = self.root / 'not-created'
                with self.assertRaisesRegex(RuntimeError, 'persistent disk'):
                    MODULE.prepare_temp_root(target)
                self.assertFalse(target.exists())

    @unittest.skipUnless(os.name == 'posix', 'POSIX private-directory permissions')
    def test_shared_directory_is_rejected_without_changing_permissions(self):
        target = self.root / 'shared'
        target.mkdir(mode=0o755)
        target.chmod(0o755)
        with self.assertRaisesRegex(PermissionError, 'private'):
            MODULE.prepare_temp_root(target)
        self.assertEqual(stat.S_IMODE(target.stat().st_mode), 0o755)

    @unittest.skipUnless(sys.platform.startswith('linux'), 'Linux filesystem detection')
    def test_mountinfo_uses_longest_mount_and_decodes_spaces(self):
        mountinfo = ('1 0 0:1 / / rw - ext4 /dev/test rw\n'
                     '2 1 0:2 / /workspace\\040disk rw - btrfs /dev/test2 rw\n'
                     '3 2 0:3 / /workspace\\040disk/ram rw - tmpfs tmpfs rw\n')
        with mock.patch.object(Path, 'read_text', return_value=mountinfo):
            self.assertEqual(MODULE._filesystem_type(Path('/workspace disk/tmp')), 'btrfs')
            self.assertEqual(MODULE._filesystem_type(Path('/workspace disk/ram/tmp')), 'tmpfs')

    @unittest.skipUnless(sys.platform.startswith('linux'), 'Linux filesystem detection')
    def test_live_system_tmp_is_rejected_when_ram_backed(self):
        if MODULE._filesystem_type(Path('/tmp').resolve()) not in MODULE.MEMORY_FILESYSTEMS:
            self.skipTest('System /tmp is not RAM-backed on this host')
        with self.assertRaisesRegex(RuntimeError, 'persistent disk'):
            MODULE.prepare_temp_root('/tmp/mwnet-do-not-create')


if __name__ == '__main__':
    unittest.main()
