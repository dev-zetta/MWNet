#!/usr/bin/env python3
"""Start an isolated, password-protected real dedicated server for discovery tests."""
import ipaddress
import os
from pathlib import Path
import secrets
import subprocess
from urllib.parse import urlsplit

os.umask(0o077)
state = Path('/state')
os.environ.update(XDG_CONFIG_HOME='/state/xdg-config', XDG_DATA_HOME='/state/xdg-data',
                  XDG_CACHE_HOME='/state/xdg-cache', OPENMW_DISABLE_CRASH_CATCHER='1')
address = os.environ['PUBLIC_ADDRESS']
if not ipaddress.ip_address(address).is_global:
    raise SystemExit('PUBLIC_ADDRESS must be a globally routable numeric address')
port = int(os.environ['PUBLIC_PORT'])
if not 1 <= port <= 65535:
    raise SystemExit('PUBLIC_PORT is out of range')
origin = os.environ.get('DISCOVERY_URL', 'https://mwnet.playit.plus')
parsed = urlsplit(origin)
if (parsed.scheme != 'https' or not parsed.hostname or parsed.path or parsed.query
        or parsed.fragment or parsed.username or parsed.password):
    raise SystemExit('DISCOVERY_URL must be an HTTPS origin')
announce = os.environ.get('ANNOUNCE', 'true')
if announce not in ('true', 'false'):
    raise SystemExit('ANNOUNCE must be true or false')

subprocess.run(['/opt/mwnet/run-mwnet-server.sh', '--prepare-only'], check=True)
password_file = state / 'access-password.txt'
if not password_file.exists():
    with password_file.open('x') as stream:
        stream.write(secrets.token_urlsafe(24) + '\n')
    password_file.chmod(0o600)
hash_file = state / 'access-password.hash'
if not hash_file.exists():
    result = subprocess.run(['/opt/mwnet/build/mwnet-server', '--hash-access-password'],
                            input=password_file.read_text(), text=True, capture_output=True)
    if result.returncode or not result.stdout.strip().startswith('$argon2id$'):
        raise SystemExit('Dedicated server could not initialize its access-password hash')
    hash_file.write_text(result.stdout.strip() + '\n')
    hash_file.chmod(0o600)
password_hash = hash_file.read_text().strip()
if not password_hash.startswith('$argon2id$') or '\n' in password_hash:
    raise SystemExit('Invalid stored password hash')
config = state / 'xdg-config/openmw/mwnet-server.cfg'
config.write_text(f'''[General]
localAddress = 0.0.0.0
publicListen = true
port = 25565
maximumPlayers = 16
hostname = MWNet registration test (temporary)
logLevel = 1
passwordHash = {password_hash}

[Plugins]
home = /state/server
plugins = serverCore.lua

[Discovery]
directoryUrl = {origin}
announce = {announce}
localTest = false
publicAddress = {address}
publicPort = {port}
''')
config.chmod(0o600)
print(f'Registration test endpoint: {address}:{port}; protocol 13; capacity 16; password required', flush=True)
os.execv('/opt/mwnet/run-mwnet-server.sh', ['/opt/mwnet/run-mwnet-server.sh', '--no-logs'])
