#!/usr/bin/env python3
"""Independent listing verifier checks with ephemeral Ed25519 keys, never a server key."""
import ctypes
import ctypes.util
import importlib.util
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('listing_verifier', ROOT / 'docker/discovery/registration-test/verify_listing.py')
VERIFY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFY)


class SignatureTests(unittest.TestCase):
    def setUp(self):
        self.sodium = ctypes.CDLL(ctypes.util.find_library('sodium') or 'libsodium.so.23')
        self.assertGreaterEqual(self.sodium.sodium_init(), 0)
        self.public = ctypes.create_string_buffer(32)
        self.secret = ctypes.create_string_buffer(64)
        self.sodium.crypto_sign_keypair.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        self.assertEqual(self.sodium.crypto_sign_keypair(self.public, self.secret), 0)
        self.sodium.sodium_memzero.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        self.addCleanup(lambda: self.sodium.sodium_memzero(self.secret, 64))
        n = lambda value: struct.pack('<I', value)
        payload = (n(1) + VERIFY.text('209.25.141.17') + n(43133)
                   + VERIFY.text('MWNet registration test (temporary)') + VERIFY.text('1.0.0-alpha.2')
                   + n(13) + n(0) + n(16) + n(1) + n(3))
        for name in ('Morrowind.esm', 'Tribunal.esm', 'Bloodmoon.esm'):
            payload += VERIFY.text(name) + n(1) + n(0x12345678)
        self.envelope = {'origin': 'https://mwnet.playit.plus', 'operation': 'announce',
                         'nonce': '12' * 32, 'publicKey': self.public.raw.hex(),
                         'payload': payload.hex(), 'signature': ''}
        message = VERIFY.DOMAIN + b''.join(VERIFY.text(self.envelope[k]) for k in ('origin', 'operation', 'nonce', 'publicKey'))
        message += n(len(payload)) + payload
        signature = ctypes.create_string_buffer(64)
        self.sodium.crypto_sign_detached.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_ulonglong, ctypes.c_void_p]
        self.assertEqual(self.sodium.crypto_sign_detached(signature, None, message, len(message), self.secret), 0)
        self.envelope['signature'] = signature.raw.hex()

    def test_valid_exact_bytes_decode_in_order(self):
        result = VERIFY.verify(self.envelope, 'https://mwnet.playit.plus')
        self.assertTrue(result['signatureVerified'])
        self.assertEqual((result['host'], result['port'], result['protocol'], result['capacity']),
                         ('209.25.141.17', 43133, 13, 16))
        self.assertTrue(result['password'])
        self.assertEqual([x['name'] for x in result['content']], ['Morrowind.esm', 'Tribunal.esm', 'Bloodmoon.esm'])

    def test_tampered_payload_signature_or_nonce_is_rejected(self):
        for field in ('payload', 'signature', 'nonce'):
            with self.subTest(field=field):
                altered = dict(self.envelope)
                value = bytearray.fromhex(altered[field])
                value[-1] ^= 1
                altered[field] = value.hex()
                with self.assertRaisesRegex(ValueError, 'signature'):
                    VERIFY.verify(altered, 'https://mwnet.playit.plus')

    def test_wrong_origin_or_operation_is_rejected(self):
        with self.assertRaises(ValueError):
            VERIFY.verify(self.envelope, 'https://different.example')
        with self.assertRaises(ValueError):
            VERIFY.verify(dict(self.envelope, operation='withdraw'), 'https://mwnet.playit.plus')


if __name__ == '__main__':
    unittest.main()
