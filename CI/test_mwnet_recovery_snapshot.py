#!/usr/bin/env python3
"""Recovery refusal, integrity and rollback-state preservation regressions."""

import hashlib
import io
import json
from pathlib import Path
import random
import tarfile
import tempfile
import unittest
from unittest.mock import patch

import mwnet_recovery_snapshot as SNAP


class RecoverySnapshotTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.state = self.root / 'state'
        (self.state / 'account').mkdir(parents=True)
        (self.state / 'world').mkdir()
        (self.state / 'account/user.json').write_bytes(b'{"credential":"fixture-only"}')
        (self.state / 'identity.key').write_bytes(b'private-fixture-bytes')
        self.backup = self.root / 'backup.tar.gz'

    def tearDown(self):
        self.temp.cleanup()

    def test_roundtrip_and_rollback_preserve_both_generations(self):
        before = SNAP.inventory(self.state)
        old_hash = SNAP.snapshot(self.state, self.backup)
        (self.state / 'account/user.json').write_bytes(b'new-state')
        newer = self.root / 'new.tar.gz'
        new_hash = SNAP.snapshot(self.state, newer)
        self.assertEqual(SNAP.restore(self.backup, old_hash, self.root / 'old'), before)
        SNAP.restore(newer, new_hash, self.root / 'new')
        self.assertEqual((self.root / 'new/account/user.json').read_bytes(), b'new-state')
        self.assertEqual((self.root / 'old/account/user.json').read_bytes(), b'{"credential":"fixture-only"}')
        self.assertEqual(self.backup.stat().st_mode & 0o777, 0o600)
        self.assertEqual((self.root / 'old/identity.key').stat().st_mode & 0o777, 0o600)
        self.assertEqual((self.root / 'old').stat().st_mode & 0o777, 0o700)

    def test_refuses_overwrite_and_leaves_old_state(self):
        value = SNAP.snapshot(self.state, self.backup)
        with self.assertRaises(ValueError):
            SNAP.snapshot(self.state, self.backup)
        with self.assertRaises(ValueError):
            SNAP.restore(self.backup, value, self.state)
        self.assertEqual(SNAP.digest(self.backup), value)

    def test_corrupt_or_incomplete_backup_does_not_publish_restore(self):
        value = SNAP.snapshot(self.state, self.backup)
        for content in (self.backup.read_bytes()[:50], b'not-a-backup'):
            broken = self.root / 'broken.tar.gz'
            broken.write_bytes(content)
            with self.assertRaises(ValueError):
                SNAP.restore(broken, value, self.root / 'restored')
            self.assertFalse((self.root / 'restored').exists())

    def test_symlink_source_refused_without_partial_backup(self):
        (self.state / 'external').symlink_to(self.root)
        with self.assertRaises(ValueError):
            SNAP.snapshot(self.state, self.backup)
        self.assertFalse(self.backup.exists())

    def test_dangling_destination_symlink_is_not_replaced(self):
        value = SNAP.snapshot(self.state, self.backup)
        destination = self.root / 'restored'
        destination.symlink_to(self.root / 'missing')
        with self.assertRaises(ValueError):
            SNAP.restore(self.backup, value, destination)
        self.assertTrue(destination.is_symlink())
        self.assertFalse((self.root / 'missing').exists())

    def test_snapshot_cannot_publish_its_own_unrestorable_compressed_size(self):
        source = self.root / 'incompressible'
        source.mkdir()
        rng = random.Random(0)
        (source / 'state').write_bytes(bytes(rng.getrandbits(8) for _ in range(4096)))
        with patch.object(SNAP, 'MAX_BYTES', 4200), self.assertRaises(ValueError):
            SNAP.snapshot(source, self.backup)
        self.assertFalse(self.backup.exists())
        self.assertFalse(list(self.root.glob('.snapshot-*')))

    def archive(self, members):
        path = self.root / 'crafted.tar.gz'
        with tarfile.open(path, 'w:gz') as t:
            for name, data in members:
                info = tarfile.TarInfo(name)
                info.size = len(data)
                t.addfile(info, io.BytesIO(data))
        return path, SNAP.digest(path)

    def test_traversal_payload_and_missing_manifest_refused(self):
        payload = b'escape'
        manifest = {'schemaVersion': 1, 'directories': [], 'bytes': len(payload),
                    'files': {'../escape': {'bytes': len(payload), 'sha256': hashlib.sha256(payload).hexdigest()}}}
        for entries in ([('payload/file', payload)], [('MANIFEST.json', json.dumps(manifest).encode()), ('payload/../escape', payload)]):
            path, value = self.archive(entries)
            with self.assertRaises(ValueError):
                SNAP.restore(path, value, self.root / 'restored')
            self.assertFalse((self.root / 'escape').exists())
            self.assertFalse((self.root / 'restored').exists())

    def test_manifest_payload_mismatch_refused_even_with_valid_archive_digest(self):
        manifest = {'schemaVersion': 1, 'directories': [], 'bytes': 3,
                    'files': {'key': {'bytes': 3, 'sha256': '0' * 64}}}
        path, value = self.archive([('MANIFEST.json', json.dumps(manifest).encode()), ('payload/key', b'bad')])
        with self.assertRaises(ValueError):
            SNAP.restore(path, value, self.root / 'restored')
        self.assertFalse((self.root / 'restored').exists())


if __name__ == '__main__':
    unittest.main()
