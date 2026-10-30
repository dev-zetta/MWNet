#!/usr/bin/env python3
"""Test the real client's opt-in control boundary without game assets or a display.

Raw fixtures, credential files and client logs remain in a private output tree.
Only control-validation.json is intended for publication. Every case must reject
before networking/world initialization, exit nonzero with the expected cause,
preserve existing files and keep the generated credential out of its output.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import signal
import socket
import subprocess
import sys
import time

MAX_LOG_BYTES = 2 * 1024 * 1024
CASE_TIMEOUT_SECONDS = 8


def _private(path: Path, text: str) -> None:
    with path.open('x', encoding='utf-8') as stream:
        stream.write(text)
    path.chmod(0o600)


def _stop(child: subprocess.Popen) -> None:
    if child.poll() is not None:
        return
    try:
        if os.name == 'posix':
            os.killpg(child.pid, signal.SIGKILL)
        else:
            child.kill()
    except ProcessLookupError:
        pass
    child.wait(timeout=5)


def _environment(profile: Path) -> dict[str, str]:
    # Neither test passwords nor unrelated inherited CI/account credentials are
    # passed through the client's environment. Preserve only loader/OS basics.
    keys = ('PATH', 'LD_LIBRARY_PATH', 'DYLD_LIBRARY_PATH', 'SYSTEMROOT', 'WINDIR',
            'SystemRoot', 'SystemDrive', 'TEMP', 'TMP', 'LANG', 'LC_ALL')
    env = {key: os.environ[key] for key in keys if key in os.environ}
    env.update(HOME=str(profile / 'home'), XDG_CONFIG_HOME=str(profile / 'config'),
               XDG_DATA_HOME=str(profile / 'data'), XDG_CACHE_HOME=str(profile / 'cache'),
               OPENMW_DISABLE_CRASH_CATCHER='1', NO_COLOR='1')
    return env


def validate_controls(client: Path, resources: Path, output: Path) -> dict:
    client, resources, output = client.resolve(), resources.resolve(), output.resolve()
    if not client.is_file() or not (resources / 'version').is_file() or not (resources / 'vfs').is_dir():
        raise ValueError('A built client and its matching resources are required')
    output.mkdir(parents=True, mode=0o700)
    output.chmod(0o700)
    secret = secrets.token_urlsafe(32)
    cases = [
        ('public-endpoint', r'loopback'),
        ('unknown-action', r'unknown.*action'),
        ('unbounded-timeout', r'bounded|deadline|timeout'),
        ('invalid-fingerprint', r'pinned|identity|fingerprint'),
        ('oversized-credential', r'bounded|private|credential|0600'),
        ('nul-credential', r'credential'),
        ('malformed-control', r'invalid.*json'),
        ('existing-result', r'new absolute|exclusiv|result'),
    ]
    posix_cases = [
        ('public-control', r'0600|private'),
        ('public-credential', r'0600|private'),
        ('fifo-control', r'regular|private|0600'),
        ('fifo-credential', r'regular|private|0600'),
        ('symlink-control', r'private|open'),
        ('symlink-result', r'new absolute|exclusiv|result'),
    ]
    if os.name == 'posix':
        cases += posix_cases
    with client.open('rb') as stream:
        client_digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    receipt = {'schemaVersion': 1, 'scope': 'native-client-gameplay-control-boundary',
               'clientSha256': client_digest,
               'requiresGameAssets': False, 'displayEnabled': False,
               'caseTimeoutSeconds': CASE_TIMEOUT_SECONDS, 'cases': [], 'outcome': 'running',
               'platformInapplicableCases': [] if os.name == 'posix' else [name for name, _ in posix_cases]}
    report = output / 'control-validation.json'
    previous_umask = os.umask(0o077)
    try:
        # Hold an unused loopback UDP port. Even a regression accepting an input
        # cannot accidentally connect to an unrelated local game service.
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as guard:
            guard.bind(('127.0.0.1', 0))
            port = guard.getsockname()[1]
            for name, expected in cases:
                profile = output / name
                profile.mkdir(mode=0o700)
                for child_dir in ('config/openmw', 'data', 'cache', 'home'):
                    (profile / child_dir).mkdir(parents=True, mode=0o700)
                _private(profile / 'config/openmw/openmw.cfg',
                         f'resources="{resources}"\ndata="{resources / "vfs"}"\n'
                         'content=MWNetControlValidation.esm\n')
                password = profile / 'account-password'
                _private(password, secret + '\n')
                control, result = profile / 'control.json', profile / 'result.jsonl'
                settings = {'schemaVersion': 1, 'host': '127.0.0.1', 'port': port,
                            'account': 'MWNetAutoControl', 'trustFingerprint': 'ed25519/' + 'A' * 43,
                            'timeoutSeconds': 20, 'accountPasswordFile': str(password),
                            'resultFile': str(result), 'commands': [{'action': 'finish'}]}
                marker = None
                if name == 'public-endpoint': settings['host'] = '192.0.2.1'
                elif name == 'unknown-action': settings['commands'].insert(0, {'action': 'unknown_unsafe'})
                elif name == 'unbounded-timeout': settings['timeoutSeconds'] = 301
                elif name == 'invalid-fingerprint': settings['trustFingerprint'] = 'ed25519/invalid'
                elif name == 'oversized-credential': password.write_text(secret * 8)
                elif name == 'nul-credential': password.write_bytes(secret.encode() + b'\0\n')
                elif name == 'public-credential': password.chmod(0o644)
                elif name == 'fifo-credential':
                    password.unlink()
                    os.mkfifo(password, 0o600)
                if name in ('existing-result', 'symlink-result'):
                    marker = profile / 'marker'
                    _private(marker, 'existing-file-must-survive\n')
                    if name == 'existing-result': settings['resultFile'] = str(marker)
                    else: result.symlink_to(marker)
                _private(control, json.dumps(settings) + '\n')
                if name == 'public-control': control.chmod(0o644)
                elif name == 'fifo-control':
                    control.unlink()
                    os.mkfifo(control, 0o600)
                elif name == 'symlink-control':
                    control.rename(profile / 'real-control.json')
                    control.symlink_to(profile / 'real-control.json')
                elif name == 'malformed-control': control.write_text('{"private":' + json.dumps(secret))
                log = profile / 'client.log'
                started = time.monotonic()
                with log.open('xb') as stream:
                    child = subprocess.Popen([str(client), '--gameplay-test-file', str(control), '--no-grab'],
                                             cwd=client.parent, env=_environment(profile), stdin=subprocess.DEVNULL,
                                             stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
                    try:
                        while child.poll() is None:
                            if time.monotonic() - started > CASE_TIMEOUT_SECONDS:
                                raise RuntimeError(name + ': client did not reject within its external deadline')
                            if log.stat().st_size > MAX_LOG_BYTES:
                                raise RuntimeError(name + ': client log exceeded its budget')
                            time.sleep(.05)
                    finally:
                        _stop(child)
                if log.stat().st_size > MAX_LOG_BYTES:
                    raise RuntimeError(name + ': client log exceeded its budget')
                text = log.read_text(errors='replace')
                if child.returncode != 1 or not re.search(expected, text, re.I):
                    raise RuntimeError(name + ': wrong native rejection status or cause; inspect the private log')
                if secret in text:
                    raise RuntimeError(name + ': client exposed the private fixture credential')
                if marker and marker.read_text() != 'existing-file-must-survive\n':
                    raise RuntimeError(name + ': client overwrote an existing file')
                if name == 'symlink-result' and (not result.is_symlink() or result.readlink() != marker):
                    raise RuntimeError(name + ': client replaced the existing result symlink')
                if name != 'symlink-result' and result.exists():
                    raise RuntimeError(name + ': rejected input created a gameplay result')
                receipt['cases'].append({'name': name, 'outcome': 'pass', 'exitCode': child.returncode,
                                         'durationSeconds': round(time.monotonic() - started, 3)})
                print('PASS: native gameplay control ' + name, flush=True)
        receipt.update(outcome='pass', credentialAbsentFromLogs=True, existingFilesPreserved=True)
    except Exception:
        receipt['outcome'] = 'fail'
        raise
    finally:
        try:
            _private(report, json.dumps(receipt, indent=2) + '\n')
        finally:
            os.umask(previous_umask)
    return receipt


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', required=True, type=Path)
    parser.add_argument('--resources', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    try:
        validate_controls(args.client, args.resources, args.output)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print('Native gameplay control validation failed: ' + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
