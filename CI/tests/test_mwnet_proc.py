#!/usr/bin/env python3
"""Procfs sampling must distinguish retired children from unreadable live ones."""
from pathlib import Path
import subprocess
import sys
import time
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'CI'))
import mwnet_proc as MODULE


def process_stat(state='R', start='123'):
    fields = [state] + ['0'] * 21
    fields[11:13] = ['50', '10']
    fields[19] = start
    return '123 (worker with ) spaces) ' + ' '.join(fields)


class ProcessSamplingTests(unittest.TestCase):
    def read(self, path, *args, **kwargs):
        if path.name == 'stat': return process_stat()
        if path.name == 'status': return 'VmRSS: 16 kB\nVmHWM: 32 kB\n'
        if path.name == 'children': return ''
        raise AssertionError(str(path))

    def test_zombie_never_requires_descriptor_access(self):
        with mock.patch.object(Path, 'read_text', return_value=process_stat('Z')), \
             mock.patch.object(Path, 'iterdir', side_effect=PermissionError(13, 'denied')) as descriptors:
            sample = MODULE.sample_process(123)
        self.assertTrue(sample['exited'])
        self.assertEqual(sample['rssBytes'], 0)
        self.assertEqual(sample['openFileDescriptors'], 0)
        descriptors.assert_not_called()

    def test_incomplete_stat_cannot_be_treated_as_an_empty_live_process(self):
        for value in ('', 'incomplete', '123 (worker) R'):
            with self.subTest(value=value), mock.patch.object(Path, 'read_text', return_value=value):
                with self.assertRaises(ValueError):
                    MODULE.sample_process(123)

    def test_exit_between_status_and_descriptor_read_is_not_a_live_access_failure(self):
        states = iter(['R', 'Z'])
        def read(path, *args, **kwargs):
            return process_stat(next(states)) if path.name == 'stat' else self.read(path)
        with mock.patch.object(Path, 'read_text', new=read), \
             mock.patch.object(Path, 'iterdir', side_effect=PermissionError(13, 'denied')):
            sample = MODULE.sample_process(123)
        self.assertTrue(sample['exited'])
        self.assertEqual(sample['rssBytes'], 0)

    def test_transient_live_permission_failure_is_retried_without_double_counting(self):
        with mock.patch.object(Path, 'read_text', new=lambda path, *a, **k: self.read(path)), \
             mock.patch.object(Path, 'iterdir', side_effect=[PermissionError(13, 'denied'), iter([Path('1'), Path('2')])]), \
             mock.patch.object(MODULE.time, 'sleep') as sleep, \
             mock.patch.object(MODULE.os, 'sysconf', return_value=100):
            sample = MODULE.sample_process(123)
        self.assertFalse(sample['exited'])
        self.assertEqual(sample['rssBytes'], 16384)
        self.assertEqual(sample['sumProcessPeakRssBytes'], 32768)
        self.assertEqual(sample['openFileDescriptors'], 2)
        self.assertEqual(sample['cpuSeconds'], .6)
        self.assertEqual(sample['permissionRetries'], 1)
        self.assertEqual(sleep.call_count, 1)

    def test_persistently_unreadable_live_process_still_fails(self):
        with mock.patch.object(Path, 'read_text', new=lambda path, *a, **k: self.read(path)), \
             mock.patch.object(Path, 'iterdir', side_effect=PermissionError(13, 'denied')) as descriptors, \
             mock.patch.object(MODULE.time, 'sleep') as sleep:
            with self.assertRaises(PermissionError):
                MODULE.sample_process(123)
        self.assertEqual(descriptors.call_count, 3)
        self.assertEqual(sleep.call_count, 2)

    def test_disappeared_or_reused_pid_is_not_counted_as_original_process(self):
        with mock.patch.object(Path, 'read_text', side_effect=FileNotFoundError):
            self.assertTrue(MODULE.sample_process(123)['exited'])
        states = iter([process_stat(start='1'), process_stat(start='2')])
        def read(path, *args, **kwargs):
            return next(states) if path.name == 'stat' else self.read(path)
        with mock.patch.object(Path, 'read_text', new=read), \
             mock.patch.object(Path, 'iterdir', return_value=iter([])):
            self.assertTrue(MODULE.sample_process(123)['exited'])

    def test_aggregate_excludes_only_retired_resources(self):
        live = {'rssBytes': 16, 'sumProcessPeakRssBytes': 32, 'cpuSeconds': .5,
                'openFileDescriptors': 3, 'exited': False, 'permissionRetries': 1}
        dead = live | {'rssBytes': 0, 'sumProcessPeakRssBytes': 0, 'cpuSeconds': 0,
                       'openFileDescriptors': 0, 'exited': True, 'permissionRetries': 0}
        with mock.patch.object(MODULE, 'descendants', return_value={1, 2}), \
             mock.patch.object(MODULE, 'sample_process', side_effect=lambda pid: live if pid == 1 else dead):
            result = MODULE.observe(1)
        self.assertEqual(result['rssBytes'], 16)
        self.assertEqual(result['openFileDescriptors'], 3)
        self.assertEqual(result['retiredProcesses'], 1)
        self.assertEqual(result['permissionRetries'], 1)

    def test_real_owned_zombie_is_handled_and_reaped(self):
        process = subprocess.Popen(['/usr/bin/true'])
        try:
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                if Path(f'/proc/{process.pid}/stat').read_text().split(')', 1)[1].split()[0] == 'Z':
                    break
                time.sleep(.001)
            else:
                self.fail('Owned child did not become a zombie')
            sample = MODULE.observe(process.pid)
            self.assertEqual(sample['retiredProcesses'], 1)
            self.assertEqual(sample['rssBytes'], 0)
            self.assertEqual(sample['openFileDescriptors'], 0)
        finally:
            process.wait(timeout=3)


if __name__ == '__main__':
    unittest.main()
