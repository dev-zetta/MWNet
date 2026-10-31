#!/usr/bin/env python3
"""Private disk-backed scratch for local builds, tests and archive extraction."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import stat
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MEMORY_FILESYSTEMS = {'tmpfs', 'ramfs', 'devtmpfs'}


def _mount_path(value: str) -> Path:
    return Path(re.sub(r'\\([0-7]{3})', lambda match: chr(int(match[1], 8)), value))


def _filesystem_type(directory: Path) -> str | None:
    if not sys.platform.startswith('linux'):
        return None
    candidates = []
    for line in Path('/proc/self/mountinfo').read_text().splitlines():
        fields = line.split()
        separator = fields.index('-')
        mount = _mount_path(fields[4])
        if directory.is_relative_to(mount):
            candidates.append((len(mount.parts), fields[separator + 1]))
    if not candidates:
        raise RuntimeError(f'Cannot determine temporary filesystem for {directory}')
    return max(candidates)[1]


def prepare_temp_root(directory: Path | str | None = None) -> Path:
    """Create private scratch; inherited system TMPDIR never selects the default."""
    selected = directory if directory is not None else os.environ.get('MWNET_TMPDIR') or ROOT / 'tmp'
    path = Path(selected).expanduser().resolve()
    if any(character in str(path) for character in ('\n', '\r')):
        raise ValueError('Temporary directory paths cannot contain line breaks')
    ancestor = path
    while not ancestor.exists():
        ancestor = ancestor.parent
    if _filesystem_type(ancestor) in MEMORY_FILESYSTEMS:
        raise RuntimeError(f'Temporary files must use persistent disk, not a RAM filesystem: {path}')
    path.mkdir(parents=True, exist_ok=True, mode=0o700)
    info = path.stat()
    if not stat.S_ISDIR(info.st_mode):
        raise ValueError(f'Temporary path is not a directory: {path}')
    if os.name == 'posix':
        if info.st_uid != os.geteuid() or stat.S_IMODE(info.st_mode) & 0o077:
            raise PermissionError(f'Temporary directory must be owned by this user and private (0700): {path}')
    # Recheck the created path in case it is an existing mount or symlink target.
    if _filesystem_type(path) in MEMORY_FILESYSTEMS:
        raise RuntimeError(f'Temporary files must use persistent disk: {path}')
    return path


def configure_temp_environment(directory: Path | str | None = None) -> Path:
    path = prepare_temp_root(directory)
    for variable in ('TMPDIR', 'TMP', 'TEMP'):
        os.environ[variable] = str(path)
    tempfile.tempdir = str(path)
    return path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--print-root', action='store_true', help='Print the validated absolute scratch path')
    parser.add_argument('--directory', type=Path, help='Use this disk directory instead of the repository default')
    args = parser.parse_args()
    try:
        print(prepare_temp_root(args.directory))
    except (OSError, RuntimeError, ValueError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
