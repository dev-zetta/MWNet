#!/usr/bin/env python3
"""Bounded procfs observations across child exit and exec transitions."""
import os
from pathlib import Path
import time


def _stat(pid):
    try:
        parts = Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)
    except (FileNotFoundError, ProcessLookupError):
        return None
    if len(parts) != 2:
        raise ValueError('Incomplete process stat observation')
    fields = parts[1].split()
    if len(fields) < 20:
        raise ValueError('Incomplete process stat observation')
    return fields


def _retired(fields):
    return fields is None or fields[0] in ('Z', 'X', 'x')


def _cpu(fields):
    return 0.0 if fields is None else (int(fields[11]) + int(fields[12])) / os.sysconf('SC_CLK_TCK')


def _exited(fields, retries):
    # Zombies have no address space or descriptors. Their final CPU counters
    # remain observable until reaping; cgroup counters also retain all CPU use.
    return {'rssBytes': 0, 'sumProcessPeakRssBytes': 0, 'cpuSeconds': _cpu(fields),
            'openFileDescriptors': 0, 'exited': True, 'permissionRetries': retries}


def sample_process(pid):
    for attempt in range(3):
        try:
            before = _stat(pid)
            if _retired(before):
                return _exited(before, attempt)
            status = dict(line.split(':', 1) for line in Path(f'/proc/{pid}/status').read_text().splitlines()
                          if ':' in line)
            fds = len(list(Path(f'/proc/{pid}/fd').iterdir()))
            after = _stat(pid)
            if _retired(after) or after[19] != before[19]:
                return _exited(before, attempt)
            return {'rssBytes': int(status.get('VmRSS', '0').split()[0]) * 1024,
                    'sumProcessPeakRssBytes': int(status.get('VmHWM', '0').split()[0]) * 1024,
                    'cpuSeconds': _cpu(after), 'openFileDescriptors': fds,
                    'exited': False, 'permissionRetries': attempt}
        except (FileNotFoundError, ProcessLookupError):
            return _exited(None, attempt)
        except PermissionError:
            current = _stat(pid)
            if _retired(current):
                return _exited(current, attempt)
            # exec can briefly change dumpability while the process stays live.
            # Persistent live-process access failures must still reject the run.
            if attempt == 2:
                raise
            time.sleep(.01)
    raise AssertionError('Unreachable process observation state')


def descendants(pid):
    result, pending = set(), [pid]
    while pending:
        current = pending.pop()
        if current in result:
            continue
        result.add(current)
        if len(result) > 128:
            raise RuntimeError('Unexpected process-tree growth')
        if _retired(_stat(current)):
            continue
        try:
            pending += [int(value) for value in Path(f'/proc/{current}/task/{current}/children').read_text().split()]
        except (FileNotFoundError, ProcessLookupError):
            pass
        except PermissionError:
            if not _retired(_stat(current)):
                raise
    return result


def observe(pid):
    result = {'rssBytes': 0, 'sumProcessPeakRssBytes': 0, 'cpuSeconds': 0.0,
              'openFileDescriptors': 0, 'retiredProcesses': 0, 'permissionRetries': 0}
    for child in descendants(pid):
        sample = sample_process(child)
        for key in ('rssBytes', 'sumProcessPeakRssBytes', 'cpuSeconds', 'openFileDescriptors', 'permissionRetries'):
            result[key] += sample[key]
        result['retiredProcesses'] += int(sample['exited'])
    return result
