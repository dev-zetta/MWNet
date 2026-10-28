#!/usr/bin/env python3
"""Verify actual directory signatures and observe a specified public game listing."""
import argparse
import base64
import ctypes
import ctypes.util
import hashlib
import http.client
import ipaddress
import json
import re
import socket
import ssl
import struct
import time
from urllib.parse import urlsplit

DOMAIN = b'MWNet discovery v1\n'
FIELDS = {'origin', 'operation', 'nonce', 'publicKey', 'payload', 'signature'}


def text(value):
    data = value.encode('utf-8')
    return struct.pack('<I', len(data)) + data


def verify(envelope, origin):
    if (set(envelope) != FIELDS or not all(isinstance(v, str) for v in envelope.values())
            or envelope['origin'] != origin or envelope['operation'] != 'announce'):
        raise ValueError('Invalid listing envelope or directory origin')
    for field, size in (('nonce', 64), ('publicKey', 64), ('signature', 128)):
        if not re.fullmatch('[0-9a-fA-F]{' + str(size) + '}', envelope[field]):
            raise ValueError('Invalid signature/key/nonce encoding')
    if len(envelope['payload']) > 524288 or not re.fullmatch('(?:[0-9a-fA-F]{2})*', envelope['payload']):
        raise ValueError('Invalid listing payload encoding')
    payload = bytes.fromhex(envelope['payload'])
    message = DOMAIN + b''.join(text(envelope[k]) for k in ('origin', 'operation', 'nonce', 'publicKey'))
    message += struct.pack('<I', len(payload)) + payload
    sodium = ctypes.CDLL(ctypes.util.find_library('sodium') or 'libsodium.so.23')
    if sodium.sodium_init() < 0:
        raise ValueError('Sodium initialization failed')
    sodium.crypto_sign_verify_detached.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_ulonglong, ctypes.c_char_p]
    if sodium.crypto_sign_verify_detached(bytes.fromhex(envelope['signature']), message, len(message), bytes.fromhex(envelope['publicKey'])):
        raise ValueError('Invalid directory listing signature')
    position = 0

    def number():
        nonlocal position
        if len(payload) - position < 4:
            raise ValueError('Truncated payload')
        value = struct.unpack_from('<I', payload, position)[0]
        position += 4
        return value

    def string(limit):
        nonlocal position
        size = number()
        if size > limit or size > len(payload) - position:
            raise ValueError('Invalid string length')
        value = payload[position:position + size].decode('utf-8')
        position += size
        if any(ord(c) < 32 or ord(c) == 127 for c in value):
            raise ValueError('Invalid text')
        return value

    if number() != 1:
        raise ValueError('Unknown payload schema')
    result = {'host': string(253), 'port': number(), 'name': string(128), 'version': string(64),
              'protocol': number(), 'players': number(), 'capacity': number(), 'password': number()}
    count = number()
    if count > 1000:
        raise ValueError('Too many content files')
    result['content'] = []
    for _ in range(count):
        name, hashes = string(256), number()
        if hashes > 50:
            raise ValueError('Too many content hashes')
        result['content'].append({'name': name, 'hashes': [number() for _ in range(hashes)]})
    if (position != len(payload) or not 1 <= result['port'] <= 65535
            or not 0 <= result['players'] <= result['capacity'] <= 65535
            or result['capacity'] == 0 or result['password'] not in (0, 1)):
        raise ValueError('Invalid listing values')
    result['password'] = bool(result['password'])
    result['fingerprint'] = 'ed25519/' + base64.urlsafe_b64encode(bytes.fromhex(envelope['publicKey'])).decode().rstrip('=')
    result['nonceDigest'] = hashlib.sha256(envelope['nonce'].encode()).hexdigest()
    result['signatureVerified'] = True
    return result


def fetch(origin, connect_ip=None):
    url = urlsplit(origin)
    if url.scheme != 'https' or not url.hostname or url.path or url.username or url.password or url.query or url.fragment:
        raise ValueError('Origin must be HTTPS without a path or credentials')
    if connect_ip:
        ipaddress.ip_address(connect_ip)
    cursor = ''
    for _ in range(10):
        context = ssl.create_default_context()
        conn = http.client.HTTPSConnection(url.hostname, url.port or 443, timeout=10, context=context)
        try:
            if connect_ip:
                raw = socket.create_connection((connect_ip, url.port or 443), timeout=10)
                conn.sock = context.wrap_socket(raw, server_hostname=url.hostname)
            conn.request('GET', '/v1/servers' + ('?after=' + cursor if cursor else ''))
            response = conn.getresponse()
            body = response.read(2 * 1024 * 1024 + 1)
            if response.status != 200 or len(body) > 2 * 1024 * 1024:
                raise ValueError('Directory response rejected')
            data = json.loads(body)
            if set(data) != {'servers', 'next'} or not isinstance(data['servers'], list) or len(data['servers']) > 100:
                raise ValueError('Invalid directory page')
            for envelope in data['servers']:
                yield verify(envelope, origin)
            cursor = data['next']
            if not cursor:
                return
            if not isinstance(cursor, str) or not re.fullmatch('[A-Za-z0-9_-]{1,64}', cursor):
                raise ValueError('Invalid directory cursor')
        finally:
            conn.close()
    raise ValueError('Directory pagination limit exceeded')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--origin', default='https://mwnet.playit.plus')
    parser.add_argument('--host', required=True)
    parser.add_argument('--port', type=int, required=True)
    parser.add_argument('--fingerprint', required=True)
    parser.add_argument('--connect-ip')
    parser.add_argument('--absent', action='store_true')
    parser.add_argument('--observe', type=int, default=0)
    parser.add_argument('--heartbeats', type=int, default=1)
    parser.add_argument('--deadline', type=int, default=120)
    args = parser.parse_args()
    started = time.monotonic()
    first = None
    nonces = set()
    while time.monotonic() - started < args.deadline + args.observe:
        listing = next((s for s in fetch(args.origin, args.connect_ip)
                        if s['host'] == args.host and s['port'] == args.port), None)
        if args.absent and listing is None:
            print(json.dumps({'absent': True, 'fingerprint': args.fingerprint}), flush=True)
            return
        if listing and not args.absent:
            if listing['fingerprint'] != args.fingerprint:
                raise ValueError('Advertised identity differs from the running server')
            if (listing['protocol'] != 13 or listing['version'] != '1.0.0-alpha.2'
                    or listing['capacity'] != 16 or not listing['password']
                    or [c['name'] for c in listing['content']] != ['Morrowind.esm', 'Tribunal.esm', 'Bloodmoon.esm']):
                raise ValueError('Advertised game metadata does not match this test')
            if first is None:
                first = time.monotonic()
            if listing['nonceDigest'] not in nonces:
                nonces.add(listing['nonceDigest'])
                print(json.dumps(dict(listing, acceptedHeartbeats=len(nonces))), flush=True)
            if time.monotonic() - first >= args.observe and len(nonces) >= args.heartbeats:
                return
        elif first is not None:
            raise ValueError('Listing disappeared during heartbeat observation')
        time.sleep(5)
    raise ValueError('Timed out waiting for the expected listing state')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, http.client.HTTPException) as error:
        raise SystemExit('Registration verification failed: ' + str(error))
