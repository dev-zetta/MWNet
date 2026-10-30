#!/usr/bin/env python3
"""Bounded snapshots for quiescent isolated acceptance, not concurrent live restore.

The destination parent must be private, owned by this UID and exclusively used
by the caller. Restored files become 0600/directories0700 under the current UID;
original Unix ownership and permissions are intentionally not reproduced.
"""

import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import tarfile
import tempfile

MAX_FILES = 10000
MAX_BYTES = 128 * 1024 * 1024
MAX_MANIFEST = 4 * 1024 * 1024


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def safe_name(name):
    path = PurePosixPath(name)
    if not name or path.is_absolute() or '\\' in name or any(p in ('', '.', '..') for p in name.split('/')):
        raise ValueError('Unsafe snapshot path')
    return path


def private_parent(destination):
    info = Path(destination).parent.lstat()
    if (not stat.S_ISDIR(info.st_mode) or info.st_uid != os.geteuid()
            or stat.S_IMODE(info.st_mode) & 0o077):
        raise ValueError('Destination requires an exclusively used private parent owned by this UID')


def inventory(root):
    root = Path(root)
    files, directories, total = {}, [], 0
    if root.is_symlink() or not root.is_dir():
        raise ValueError('State must be a real directory')
    for path in sorted(root.rglob('*')):
        name = path.relative_to(root).as_posix()
        safe_name(name)
        info = path.lstat()
        if stat.S_ISDIR(info.st_mode):
            directories.append(name)
        elif stat.S_ISREG(info.st_mode):
            total += info.st_size
            files[name] = {'bytes': info.st_size, 'sha256': digest(path)}
        else:
            raise ValueError('Snapshot refuses links and special files')
        if len(files) + len(directories) > MAX_FILES or total > MAX_BYTES:
            raise ValueError('Snapshot resource limit exceeded')
    return {'schemaVersion': 1, 'files': files, 'directories': directories, 'bytes': total}


def snapshot(source, destination):
    """Caller must stop writers first. Return an external integrity digest."""
    source, destination = Path(source), Path(destination)
    private_parent(destination)
    if destination.exists() or destination.is_symlink():
        raise ValueError('Existing snapshot is never overwritten')
    manifest = inventory(source)
    raw = json.dumps(manifest, sort_keys=True, separators=(',', ':')).encode()
    if len(raw) > MAX_MANIFEST:
        raise ValueError('Oversized manifest')
    temporary = None
    try:
        handle, temporary = tempfile.mkstemp(prefix='.snapshot-', dir=destination.parent)
        os.close(handle)
        os.chmod(temporary, 0o600)
        with tarfile.open(temporary, 'w:gz') as archive:
            import io
            info = tarfile.TarInfo('MANIFEST.json')
            info.size, info.mode = len(raw), 0o600
            archive.addfile(info, io.BytesIO(raw))
            for name, record in manifest['files'].items():
                path = source / name
                info = tarfile.TarInfo('payload/' + name)
                info.size, info.mode = record['bytes'], 0o600
                with path.open('rb') as stream:
                    archive.addfile(info, stream)
        if inventory(source) != manifest:
            raise ValueError('State changed during backup; stop its writers')
        if Path(temporary).stat().st_size > MAX_BYTES:
            raise ValueError('Compressed backup exceeds restore size limit')
        with open(temporary, 'rb') as stream:
            os.fsync(stream.fileno())
        # link() refuses races with an existing destination, unlike replace().
        os.link(temporary, destination)
        os.unlink(temporary)
        temporary = None
        descriptor = os.open(destination.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)
        return digest(destination)
    finally:
        if temporary is not None:
            Path(temporary).unlink(missing_ok=True)


def restore(archive_path, expected_digest, destination):
    """Verify into a new sibling directory, then atomically publish it; never overwrite."""
    archive_path, destination = Path(archive_path), Path(destination)
    private_parent(destination)
    if destination.exists() or destination.is_symlink():
        raise ValueError('Restore destination must not exist; retain current state for rollback')
    if archive_path.stat().st_size > MAX_BYTES or digest(archive_path) != expected_digest:
        raise ValueError('Backup digest or size mismatch')
    stage = Path(tempfile.mkdtemp(prefix='.restore-', dir=destination.parent))
    stage.chmod(0o700)
    try:
        with tarfile.open(archive_path, 'r:gz') as archive:
            members, expanded = [], 0
            for member in archive:
                expanded += member.size
                members.append(member)
                if (len(members) > MAX_FILES + 1 or member.size < 0
                        or expanded > MAX_BYTES + MAX_MANIFEST or not member.isreg()):
                    raise ValueError('Backup entry/type/expansion limit exceeded')
            names = [m.name for m in members]
            if len(names) != len(set(names)) or names.count('MANIFEST.json') != 1:
                raise ValueError('Missing or duplicate manifest/entries')
            entry = archive.getmember('MANIFEST.json')
            if not entry.isreg() or entry.size > MAX_MANIFEST:
                raise ValueError('Invalid manifest')
            manifest = json.loads(archive.extractfile(entry).read())
            if manifest.get('schemaVersion') != 1 or not isinstance(manifest.get('files'), dict):
                raise ValueError('Unsupported backup manifest')
            directories = manifest.get('directories')
            if not isinstance(directories, list) or len(directories) + len(manifest['files']) > MAX_FILES:
                raise ValueError('Invalid directory manifest')
            total = 0
            for name in directories:
                path = stage / safe_name(name)
                path.mkdir(mode=0o700, parents=True, exist_ok=True)
            expected = {'MANIFEST.json'} | {'payload/' + n for n in manifest['files']}
            if set(names) != expected:
                raise ValueError('Backup contents do not match manifest')
            for name, record in manifest['files'].items():
                path = stage / safe_name(name)
                info = archive.getmember('payload/' + name)
                if not info.isreg() or info.size != record['bytes'] or info.size < 0:
                    raise ValueError('Invalid backup file')
                total += info.size
                if total > MAX_BYTES:
                    raise ValueError('Backup expansion limit exceeded')
                path.parent.mkdir(parents=True, mode=0o700, exist_ok=True)
                with path.open('xb') as target, archive.extractfile(info) as source:
                    path.chmod(0o600)
                    shutil.copyfileobj(source, target, length=65536)
                    target.flush()
                    os.fsync(target.fileno())
                if digest(path) != record['sha256']:
                    raise ValueError('Restored file digest mismatch')
            if total != manifest['bytes'] or inventory(stage) != manifest:
                raise ValueError('Restored inventory mismatch')
        # renameat2(RENAME_NOREPLACE) is not portable; parent is private to this
        # isolated procedure and no other writer may operate on its destinations.
        if destination.exists() or destination.is_symlink():
            raise ValueError('Restore destination appeared during verification')
        os.rename(stage, destination)
        descriptor = os.open(destination.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)
        return manifest
    finally:
        if stage.exists():
            shutil.rmtree(stage)
