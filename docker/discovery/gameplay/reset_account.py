#!/usr/bin/env python3
"""Offline account-password administration; never accepts passwords in arguments."""
import argparse
import ctypes
import ctypes.util
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

LOWERCASE = bytes.maketrans(b'ABCDEFGHIJKLMNOPQRSTUVWXYZ', b'abcdefghijklmnopqrstuvwxyz')


def canonical(account):
    value = account.encode('utf-8')
    if not 1 <= len(value) <= 64 or any(c < 32 or c == 127 for c in value):
        raise ValueError('Invalid account name')
    # Match the native store's byte-wise ASCII tolower, including UTF-8 names.
    return value.translate(LOWERCASE)


def verify_password(encoded, password):
    sodium = ctypes.CDLL(ctypes.util.find_library('sodium') or 'libsodium.so.23')
    if sodium.sodium_init() < 0:
        raise ValueError('Sodium initialization failed')
    sodium.crypto_pwhash_str_verify.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_ulonglong]
    return sodium.crypto_pwhash_str_verify(encoded.encode('ascii'), password, len(password)) == 0


def make_hash(password, binary):
    if not 1 <= len(password) <= 128 or b'\n' in password or b'\r' in password or b'\0' in password:
        raise ValueError('Password must contain 1 to 128 bytes without line breaks or NUL')
    result = subprocess.run([str(binary), '--hash-access-password'], input=password + b'\n',
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
    encoded = result.stdout.decode('ascii', errors='strict').strip()
    if result.returncode or not encoded.startswith('$argon2id$') or not verify_password(encoded, password):
        raise ValueError('Native Argon2id generation or verification failed')
    return encoded


def reset(directory, backups, account, encoded, create_if_missing=False):
    name = canonical(account)
    digest = hashlib.sha256(name).hexdigest()
    path = directory / (digest + '.json')
    if directory.is_symlink() or path.is_symlink():
        raise ValueError('Refusing a symlink credential path')
    exists = path.exists()
    if exists:
        if not path.is_file() or path.stat().st_size > 1024 * 1024:
            raise ValueError('Invalid account record size/type')
        original = path.read_bytes()
        record = json.loads(original)
        if canonical(record['login']['name']) != name:
            raise ValueError('Credential record account name mismatch')
    elif create_if_missing:
        original = None
        record = {'login': {'name': account}}
    else:
        raise ValueError('Account has no native credential record')
    if not re.fullmatch(r'\$argon2id\$[!-~]{1,116}', encoded):
        raise ValueError('Invalid encoded Argon2id credential')
    login = record['login']
    login.update(schemaVersion=1, passwordScheme='argon2id-v1', passwordHash=encoded)
    for key in ('password', 'passwordSalt'):
        login.pop(key, None)
    payload = (json.dumps(record, ensure_ascii=False, separators=(',', ':')) + '\n').encode('utf-8')
    if len(payload) > 1024 * 1024:
        raise ValueError('Updated account record exceeds its limit')
    if original is not None:
        backups.mkdir(mode=0o700, parents=True, exist_ok=True)
        stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
        backup = backups / (digest + '.' + stamp + '.json')
        with backup.open('xb') as stream:
            os.chmod(backup, 0o600)
            stream.write(original)
            stream.flush()
            os.fsync(stream.fileno())
    directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix='.' + digest + '.reset-', dir=directory)
    try:
        with os.fdopen(descriptor, 'wb') as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        parent = os.open(directory, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(parent)
        finally:
            os.close(parent)
    finally:
        Path(temporary).unlink(missing_ok=True)
    return 'reset' if exists else 'created'


def main():
    os.umask(0o077)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--account', required=True)
    parser.add_argument('--create-if-missing', action='store_true')
    args = parser.parse_args()
    canonical(args.account)
    password = bytearray(sys.stdin.buffer.read(131))
    try:
        if password.endswith(b'\n'):
            password.pop()
            if password.endswith(b'\r'):
                password.pop()
        encoded = make_hash(bytes(password), Path('/opt/mwnet/build/mwnet-server'))
        status = reset(Path('/state/server/data/account'), Path('/state/admin-backups/account-password'),
                       args.account, encoded, args.create_if_missing)
        print(f'PASS: account credential {status} for {args.account}; Argon2id verified; gameplay profile preserved.')
    finally:
        password[:] = b'\0' * len(password)


if __name__ == '__main__':
    try:
        main()
    except (KeyError, TypeError, UnicodeError, json.JSONDecodeError):
        raise SystemExit('Account reset failed: invalid account metadata.')
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        # Our validation errors and filesystem/process diagnostics contain no
        # password or encoded credential; native stdout/stderr stays captured.
        raise SystemExit('Account reset failed: ' + str(error))
