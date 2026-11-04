#!/usr/bin/env python3
"""Private, bounded, fsynced host observations; never reads process arguments."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import fcntl
import json
import math
import os
from pathlib import Path
import re
import secrets
import signal
import stat
import subprocess
import time

MIB = 1024 ** 2


def read(path, maximum=65536):
    with Path(path).open() as stream:
        value = stream.read(maximum + 1)
    if len(value) > maximum:
        raise ValueError('Oversized host observation')
    return value


def key_numbers(path, wanted=None, kib=False):
    result = {}
    for line in read(path).splitlines():
        fields = line.replace(':', '').split()
        if len(fields) >= 2 and (wanted is None or fields[0] in wanted):
            result[fields[0]] = int(fields[1]) * (1024 if kib else 1)
    return result


def snapshot(proc=Path('/proc'), sys=Path('/sys'), disk=Path('/var/log/mwnet-host')):
    memory = key_numbers(proc / 'meminfo', ('MemTotal', 'MemAvailable', 'MemFree', 'Cached', 'Shmem',
                                         'SwapTotal', 'SwapFree', 'Slab', 'Dirty', 'Writeback'), kib=True)
    cpu = next(line.split()[1:] for line in read(proc / 'stat').splitlines() if line.startswith('cpu '))
    load = [float(value) for value in read(proc / 'loadavg').split()[:3]]
    if any(not math.isfinite(value) for value in load):
        raise ValueError('Invalid host load')
    result = {'schemaVersion': 1, 'utc': datetime.now(timezone.utc).isoformat(),
              'bootId': read(proc / 'sys/kernel/random/boot_id').strip(),
              'uptimeSeconds': float(read(proc / 'uptime').split()[0]),
              'memoryBytes': memory, 'cpuTicks': [int(value) for value in cpu], 'loadAverage': load,
              'vmstat': key_numbers(proc / 'vmstat', ('oom_kill', 'pswpin', 'pswpout', 'pgmajfault')),
              'diskAvailableBytes': os.statvfs(disk).f_bavail * os.statvfs(disk).f_frsize,
              'pressure': {}, 'temperatures': [], 'nvmeDiskCounters': {}, 'containerCgroups': {}}
    for name in ('cpu', 'memory', 'io'):
        path = proc / 'pressure' / name
        if path.exists():
            result['pressure'][name] = read(path, 4096).strip()
    for path in sorted((sys / 'class/hwmon').glob('hwmon*'))[:32]:
        try:
            name = read(path / 'name', 128).strip()
            for sensor in sorted(path.glob('temp*_input'))[:16]:
                value = int(read(sensor, 128))
                result['temperatures'].append({'chip': name, 'sensor': sensor.name,
                                               'milliCelsius': value, 'valid': -50000 <= value <= 150000})
        except (OSError, ValueError):
            continue
    for line in read(proc / 'diskstats').splitlines():
        fields = line.split()
        if len(fields) > 3 and re.fullmatch(r'nvme[0-9]+n[0-9]+', fields[2]):
            result['nvmeDiskCounters'][fields[2]] = [int(value) for value in fields[3:]]
    scopes = sorted((sys / 'fs/cgroup/system.slice').glob('docker-*.scope'))[:32]
    for scope in scopes:
        try:
            result['containerCgroups'][scope.name] = {
                'memoryCurrentBytes': int(read(scope / 'memory.current', 128)),
                'memoryPeakBytes': int(read(scope / 'memory.peak', 128)),
                'memoryEvents': key_numbers(scope / 'memory.events'),
                'cpu': key_numbers(scope / 'cpu.stat'), 'pids': int(read(scope / 'pids.current', 128))}
        except (OSError, ValueError):
            continue  # Container exits/restarts are expected; host data stays mandatory.
    return result


def private_disk_directory(path):
    path = Path(path)
    if path.is_symlink() or not path.is_dir():
        raise ValueError('Use an existing private disk directory')
    info = path.stat()
    if info.st_uid != os.geteuid() or info.st_mode & 0o077:
        raise ValueError('Observation directory must be private and owned by the logger')
    filesystem = subprocess.check_output(['stat', '-f', '-c', '%T', str(path)], text=True, timeout=5).strip()
    if filesystem in ('tmpfs', 'ramfs'):
        raise ValueError('Host observations require persistent disk')


class DurableRing:
    def __init__(self, directory, maximum=16 * MIB, files=8):
        self.directory = Path(directory)
        private_disk_directory(self.directory)
        if not 1024 <= maximum <= 32 * MIB or not 2 <= files <= 16:
            raise ValueError('Invalid bounded log retention')
        self.maximum, self.files = maximum, files
        self.path = self.directory / 'host.jsonl'
        self.stream = None
        self.lock = os.open(self.directory / 'collector.lock', os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
        try:
            lock_info = os.fstat(self.lock)
            if not stat.S_ISREG(lock_info.st_mode) or lock_info.st_nlink != 1 or lock_info.st_uid != os.geteuid():
                raise ValueError('Unsafe collector lock')
            fcntl.flock(self.lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BaseException:
            os.close(self.lock)
            raise

    def sync_directory(self):
        descriptor = os.open(self.directory, os.O_DIRECTORY | os.O_RDONLY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)

    def open(self):
        descriptor = os.open(self.path, os.O_APPEND | os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
        info = os.fstat(descriptor)
        if not stat.S_ISREG(info.st_mode) or info.st_uid != os.geteuid() or info.st_nlink != 1:
            os.close(descriptor)
            raise ValueError('Unsafe observation file')
        os.fchmod(descriptor, 0o600)
        if info.st_size and os.pread(descriptor, 1, info.st_size - 1) != b'\n':
            os.write(descriptor, b'\n')  # Preserve a torn record, separating the next observation.
            os.fsync(descriptor)
        self.stream = os.fdopen(descriptor, 'ab', buffering=0)
        self.sync_directory()

    def close(self):
        if self.stream is not None:
            self.stream.flush()
            os.fsync(self.stream.fileno())
            self.stream.close()
            self.stream = None

    def finish(self):
        self.close()
        if self.lock is not None:
            os.close(self.lock)
            self.lock = None

    def write(self, value):
        payload = (json.dumps(value, separators=(',', ':'), allow_nan=False) + '\n').encode()
        if len(payload) > min(self.maximum, 128 * 1024):
            raise ValueError('Oversized observation row')
        if self.stream is None:
            self.open()
        if os.fstat(self.stream.fileno()).st_size + len(payload) > self.maximum:
            self.close()
            for index in range(self.files - 1, 0, -1):
                old = self.path if index == 1 else self.directory / f'host.{index - 1}.jsonl'
                new = self.directory / f'host.{index}.jsonl'
                if old.exists() or old.is_symlink():
                    if old.is_symlink() or not old.is_file() or old.stat().st_nlink != 1:
                        raise ValueError('Unsafe rotated observation file')
                    old.replace(new)
            self.sync_directory()
            self.open()
        remaining = memoryview(payload)
        while remaining:
            written = self.stream.write(remaining)
            if not written:
                raise OSError('Incomplete observation write')
            remaining = remaining[written:]
        os.fsync(self.stream.fileno())


def write_status(path, value):
    temporary = path.with_name(path.name + '.' + secrets.token_hex(8) + '.new')
    descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o640)
    try:
        os.fchmod(descriptor, 0o640)
        with os.fdopen(descriptor, 'w') as stream:
            json.dump(value, stream, allow_nan=False)
            stream.write('\n')
            stream.flush()
            os.fsync(stream.fileno())
        temporary.replace(path)
        directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, default=Path('/var/log/mwnet-host'))
    parser.add_argument('--interval', type=float, default=5)
    parser.add_argument('--once', action='store_true')
    parser.add_argument('--status-file', type=Path)
    args = parser.parse_args()
    if not 1 <= args.interval <= 60:
        parser.error('interval must be between one and sixty seconds')
    stop = False

    def stopping(_signum, _frame):
        nonlocal stop
        stop = True

    for number in (signal.SIGTERM, signal.SIGINT):
        signal.signal(number, stopping)
    ring = DurableRing(args.directory)
    try:
        while not stop:
            started = time.monotonic()
            value = snapshot(disk=args.directory)
            ring.write(value)
            if args.status_file:
                write_status(args.status_file, value)
            if args.once:
                break
            while not stop and time.monotonic() - started < args.interval:
                time.sleep(min(.25, args.interval))
    finally:
        ring.finish()


if __name__ == '__main__':
    main()
