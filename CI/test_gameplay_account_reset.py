#!/usr/bin/env python3
"""Account reset integrity, private backups, atomicity and native hash transport."""
import ctypes
import ctypes.util
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('account_reset', ROOT / 'docker/discovery/gameplay/reset_account.py')
RESET = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RESET)


class ResetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sodium = ctypes.CDLL(ctypes.util.find_library('sodium') or 'libsodium.so.23')
        assert sodium.sodium_init() >= 0
        sodium.crypto_pwhash_str_alg.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_ulonglong,
                                                ctypes.c_ulonglong, ctypes.c_size_t, ctypes.c_int]
        cls.password = b'fixture-only-new-password'
        output = ctypes.create_string_buffer(128)
        assert sodium.crypto_pwhash_str_alg(output, cls.password, len(cls.password), 2, 8 * 1024 * 1024, 2) == 0
        cls.encoded = output.value.decode('ascii')

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.directory = self.root / 'account'
        self.directory.mkdir(mode=0o700)
        self.backups = self.root / 'admin-backups'
        self.name = 'FixturePlayer'
        digest = hashlib.sha256(b'fixtureplayer').hexdigest()
        self.path = self.directory / (digest + '.json')
        self.original = {'login': {'name': self.name, 'passwordHash': 'old-credential',
                                   'passwordSalt': 'old-salt', 'custom': 42}, 'metadata': {'kept': True}}
        self.path.write_text(json.dumps(self.original))
        self.profile = self.root / 'player.json'
        self.profile.write_text('{"character":"preserve this"}\n')

    def test_reset_preserves_profile_and_noncredential_fields(self):
        original = self.path.read_bytes()
        profile = self.profile.read_bytes()
        self.assertEqual(RESET.reset(self.directory, self.backups, self.name, self.encoded), 'reset')
        changed = json.loads(self.path.read_text())
        self.assertEqual(changed['metadata'], self.original['metadata'])
        self.assertEqual(changed['login']['custom'], 42)
        self.assertEqual(changed['login']['passwordScheme'], 'argon2id-v1')
        self.assertEqual(changed['login']['schemaVersion'], 1)
        self.assertNotIn('passwordSalt', changed['login'])
        self.assertTrue(RESET.verify_password(changed['login']['passwordHash'], self.password))
        self.assertFalse(RESET.verify_password(changed['login']['passwordHash'], b'wrong-fixture'))
        self.assertEqual(self.profile.read_bytes(), profile)
        backup = next(self.backups.iterdir())
        self.assertEqual(backup.read_bytes(), original)
        self.assertEqual(backup.stat().st_mode & 0o777, 0o600)
        self.assertEqual(self.path.stat().st_mode & 0o777, 0o600)

    def test_atomic_replace_failure_preserves_old_record(self):
        original = self.path.read_bytes()
        with patch.object(RESET.os, 'replace', side_effect=OSError('fixture failure')):
            with self.assertRaises(OSError):
                RESET.reset(self.directory, self.backups, self.name, self.encoded)
        self.assertEqual(self.path.read_bytes(), original)
        self.assertEqual(list(self.directory.iterdir()), [self.path])

    def test_missing_account_requires_explicit_creation(self):
        self.path.unlink()
        with self.assertRaisesRegex(ValueError, 'no native'):
            RESET.reset(self.directory, self.backups, self.name, self.encoded)
        self.assertEqual(RESET.reset(self.directory, self.backups, self.name, self.encoded, True), 'created')
        self.assertEqual(json.loads(self.path.read_text())['login']['name'], self.name)

    def test_mismatched_name_or_symlink_is_rejected(self):
        self.path.write_text(json.dumps({'login': {'name': 'AnotherPlayer'}}))
        with self.assertRaisesRegex(ValueError, 'mismatch'):
            RESET.reset(self.directory, self.backups, self.name, self.encoded)
        self.path.unlink()
        self.path.symlink_to(self.profile)
        with self.assertRaisesRegex(ValueError, 'symlink'):
            RESET.reset(self.directory, self.backups, self.name, self.encoded)

    def test_native_hash_password_uses_stdin_only(self):
        response = subprocess.CompletedProcess([], 0, (self.encoded + '\n').encode(), b'')
        with patch.object(RESET.subprocess, 'run', return_value=response) as run:
            self.assertEqual(RESET.make_hash(self.password, Path('/native-server')), self.encoded)
        self.assertEqual(run.call_args.args[0], ['/native-server', '--hash-access-password'])
        self.assertEqual(run.call_args.kwargs['input'], self.password + b'\n')
        with patch.object(RESET.subprocess, 'run', return_value=response):
            with self.assertRaisesRegex(ValueError, 'verification'):
                RESET.make_hash(b'different-fixture', Path('/native-server'))

    def test_canonical_name_matches_native_ascii_byte_rules(self):
        self.assertEqual(RESET.canonical('EXAMPLE'), b'example')
        self.assertEqual(RESET.canonical('ÄBC'), 'Äbc'.encode())
        for invalid in ('', 'a\nb', '\0', 'x' * 65):
            with self.subTest(invalid=repr(invalid)), self.assertRaises(ValueError):
                RESET.canonical(invalid)


if __name__ == '__main__':
    unittest.main()
