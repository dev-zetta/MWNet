#!/usr/bin/env python3
"""Persistently disable systemd sleep on the reviewed NUC; preserve SSH/services."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import secrets
import stat
from datetime import datetime, timezone

from configure import FileTransaction, atomic_file, command, private_directory

STATE = Path('/var/lib/mwnet-host-no-sleep')
TARGETS = ('sleep.target', 'suspend.target', 'hibernate.target', 'hybrid-sleep.target', 'suspend-then-hibernate.target')
SLEEP = b'''# MWNet always-on server policy; no automatic or requested system sleep.
[Sleep]
AllowSuspend=no
AllowHibernation=no
AllowHybridSleep=no
AllowSuspendThenHibernate=no
'''
LOGIN = b'''# Effective on the next logind start; target masks enforce the policy now.
[Login]
IdleAction=ignore
HandleSuspendKey=ignore
HandleHibernateKey=ignore
HandleLidSwitch=ignore
HandleLidSwitchExternalPower=ignore
HandleLidSwitchDocked=ignore
'''

def paths(root=Path('/')):
    return [(root / 'etc/systemd/sleep.conf.d/90-mwnet-no-sleep.conf', SLEEP, 0o644),
            (root / 'etc/systemd/logind.conf.d/90-mwnet-no-sleep.conf', LOGIN, 0o644)]

def mask_states(root=Path('/')):
    values = {}
    for unit in TARGETS:
        path = root / 'etc/systemd/system' / unit
        if path.is_symlink():
            if os.readlink(path) != '/dev/null':
                raise ValueError('Preserve unexpected existing target override: ' + unit)
            values[unit] = 'masked'
        elif path.exists():
            raise ValueError('Preserve existing regular target override: ' + unit)
        else:
            values[unit] = 'absent'
    return values

def verify():
    units = {}
    for unit in TARGETS:
        result = command(['systemctl', 'show', unit, '-p', 'LoadState', '-p', 'UnitFileState']).stdout
        values = dict(line.split('=', 1) for line in result.splitlines() if '=' in line)
        if values.get('LoadState') != 'masked' or values.get('UnitFileState') != 'masked':
            raise ValueError('Persistent sleep target mask is not active: ' + unit)
        units[unit] = values
    capabilities = {}
    for operation in ('CanSuspend', 'CanHibernate', 'CanHybridSleep', 'CanSuspendThenHibernate'):
        result = command(['busctl', 'call', 'org.freedesktop.login1', '/org/freedesktop/login1',
                          'org.freedesktop.login1.Manager', operation]).stdout.strip()
        if result not in ('s "no"', 's "na"'):
            raise ValueError('Login manager still offers sleep: ' + operation)
        capabilities[operation] = result
    return {'status': 'sleep-disabled', 'persistentTargetMasks': units, 'loginManagerCapabilities': capabilities,
            'noSuspendOrRebootTestPerformed': True, 'productionAndLoginServicesNotRestarted': True}

def restore(transaction, previous, root=Path('/')):
    # Validate the entire restore before touching a single file or mask.
    transaction.check_restorable()
    current = mask_states(root)
    for unit, state in previous.items():
        if state not in ('absent', 'masked') or current[unit] not in ('absent', 'masked'):
            raise ValueError('Unexpected mask restoration state')
    for unit, state in previous.items():
        if state == 'absent':
            (root / 'etc/systemd/system' / unit).unlink(missing_ok=True)
    transaction.restore()

def apply(root=Path('/'), state=STATE, verifier=verify, reload_manager=None):
    reload_manager = reload_manager or (lambda: command(['systemctl', 'daemon-reload']))
    changes = paths(root)
    previous = mask_states(root)
    for path, data, _ in changes:
        if path.exists() and path.read_bytes() != data:
            raise ValueError('Preserve independently edited MWNet sleep policy')
    if all(path.is_file() and not path.is_symlink() and path.read_bytes() == data for path, data, _ in changes) and all(v == 'masked' for v in previous.values()):
        return dict(verifier(), alreadyApplied=True)
    backup = state / 'backups' / (datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '-' + secrets.token_hex(4))
    transaction = FileTransaction(backup)
    transaction.prepare(changes)
    atomic_file(backup / 'masks.json', (json.dumps(previous, indent=2) + '\n').encode(), 0o600)
    try:
        for path, data, mode in changes:
            atomic_file(path, data, mode)
        for unit, old in previous.items():
            if old == 'absent':
                (root / 'etc/systemd/system' / unit).symlink_to('/dev/null')
        reload_manager()
        result = verifier()
        return dict(result, backup=str(backup), appliedFileHashes={str(p): hashlib.sha256(data).hexdigest() for p, data, _ in changes})
    except BaseException:
        restore(transaction, previous, root)
        reload_manager()
        raise

def rollback(backup):
    backup = backup.resolve()
    if (not backup.is_relative_to(STATE / 'backups') or backup.stat().st_uid != 0
            or backup.stat().st_mode & 0o077):
        raise ValueError('Use the protected backup from this no-sleep profile')
    transaction = FileTransaction(backup)
    transaction.entries = json.loads((backup / 'files.json').read_text())
    if {entry['path'] for entry in transaction.entries} != {str(p) for p, _, _ in paths()}:
        raise ValueError('Unexpected configuration rollback inventory')
    previous = json.loads((backup / 'masks.json').read_text())
    if set(previous) != set(TARGETS):
        raise ValueError('Unexpected mask rollback inventory')
    if any(value != 'masked' for value in mask_states().values()):
        raise ValueError('Preserve independent sleep target unmasking before rollback')
    restore(transaction, previous)
    command(['systemctl', 'daemon-reload'])
    return {'status': 'no-sleep-profile-rolled-back', 'backup': str(backup), 'productionAndLoginServicesNotRestarted': True}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['apply', 'verify', 'rollback'])
    parser.add_argument('--backup', type=Path)
    args = parser.parse_args()
    if args.action == 'rollback' and args.backup is None:
        parser.error('rollback requires the exact --backup from the apply report')
    if os.geteuid() != 0 or Path('/sys/class/dmi/id/product_name').read_text().strip() != 'NUC8i5BEH':
        raise ValueError('Use interactive sudo on the reviewed NUC8i5BEH')
    private_directory(STATE)
    descriptor = os.open(STATE / 'profile.lock', os.O_WRONLY | os.O_CREAT | os.O_NOFOLLOW, 0o600)
    with os.fdopen(descriptor, 'w') as lock:
        status = os.fstat(lock.fileno())
        if not stat.S_ISREG(status.st_mode) or status.st_uid != 0 or status.st_nlink != 1:
            raise ValueError('Unsafe no-sleep profile lock')
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        result = apply() if args.action == 'apply' else (verify() if args.action == 'verify' else rollback(args.backup))
    print(json.dumps(result, indent=2))

if __name__ == '__main__':
    main()
