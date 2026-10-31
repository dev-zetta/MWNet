#!/usr/bin/env python3
"""Fail-closed verdict tests for the new lean overnight profile."""
import importlib.util
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'CI'))
from mwnet_temp import prepare_temp_root

spec = importlib.util.spec_from_file_location('overnight', ROOT / 'CI/run_mwnet_overnight.py')
MODULE = importlib.util.module_from_spec(spec)
spec.loader.exec_module(MODULE)
COMMIT = 'd' * 40


def native_report():
    return {'status': 'passed', 'scenariosComplete': True, 'commit': COMMIT,
            'clients': 16, 'simulatedLatencyMilliseconds': 75,
            'simulatedPacketLossPercent': 2, 'addressSanitizerEnabled': False,
            'cycles': 100, 'elapsedSeconds': 28801, 'maximumQueueDepth': 16,
            'cleanupVerifiedCycles': 100, 'finalConnectionMetricEntries': 0,
            'finalCombatEntries': 0, 'finalInventoryEntries': 0,
            'finalJusticeEntries': 0, 'finalLifecycleEntries': 0,
            'residentMemorySamplesBytes': [16 * MODULE.MIB] * 100}


class OvernightVerdictTests(unittest.TestCase):
    def check_report(self, report, preflight=False):
        return MODULE.validate_native(report, 28800, 16, 75, 2, COMMIT, preflight)

    def test_settled_allocation_plateau_passes(self):
        values = [8 * MODULE.MIB] * 25 + [16 * MODULE.MIB] * 75
        self.assertTrue(MODULE.idle_memory_assessment(values, 28800)['accepted'])

    def test_sustained_native_growth_fails(self):
        values = [16 * MODULE.MIB + i * 96 * 1024 for i in range(120)]
        self.assertFalse(MODULE.idle_memory_assessment(values, 28800)['accepted'])

    def test_large_unexplained_post_warmup_step_fails(self):
        values = [16 * MODULE.MIB] * 90 + [28 * MODULE.MIB] * 30
        self.assertFalse(MODULE.idle_memory_assessment(values, 28800)['accepted'])

    def test_small_allocator_noise_is_reported_without_percent_only_failure(self):
        values = [16 * MODULE.MIB + i * 4096 for i in range(120)]
        report = MODULE.idle_memory_assessment(values, 28800)
        self.assertTrue(report['accepted'])
        self.assertGreater(report['lastMinusFirstWindowBytes'], 0)

    def test_missing_short_or_invalid_memory_samples_fail(self):
        for values in ([], [1] * 10, [0] * 100, [float('nan')] * 100):
            with self.subTest(values=values[:1]), self.assertRaises(ValueError):
                MODULE.idle_memory_assessment(values, 28800)

    def test_short_preflight_cannot_accept_overnight_memory(self):
        result = MODULE.idle_memory_assessment([1234], 30, True)
        self.assertFalse(result['accepted'])

    def test_complete_exact_native_workload_passes(self):
        self.assertTrue(self.check_report(native_report())['accepted'])

    def test_wrong_source_failed_incomplete_or_short_report_fails(self):
        for change in ({'commit': 'c' * 40}, {'status': 'failed'}, {'scenariosComplete': False},
                       {'elapsedSeconds': 28799}, {'cycles': 99}, {'clients': 8},
                       {'addressSanitizerEnabled': True}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                self.check_report(native_report() | change)

    def test_any_retained_or_unproven_cleanup_fails(self):
        for key in ('cleanupVerifiedCycles', 'finalConnectionMetricEntries', 'finalCombatEntries',
                    'finalInventoryEntries', 'finalJusticeEntries', 'finalLifecycleEntries'):
            for value in (None, 1):
                with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                    self.check_report(native_report() | {key: value})

    def test_observed_queue_limit_fails(self):
        with self.assertRaises(ValueError):
            self.check_report(native_report() | {'maximumQueueDepth': 17})

    def test_nonfinite_or_wrong_type_elapsed_queue_and_counts_fail(self):
        for key in ('elapsedSeconds', 'maximumQueueDepth', 'cycles', 'cleanupVerifiedCycles', 'finalCombatEntries'):
            for value in (float('nan'), float('inf'), float('-inf'), '30000', None, False):
                with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                    self.check_report(native_report() | {key: value})

    def test_supervisor_actual_wall_time_cannot_be_forged_by_component_json(self):
        MODULE.require_elapsed(28800, 28800)
        for elapsed in (28799.9, 0, float('nan'), float('inf'), '28800'):
            with self.subTest(elapsed=elapsed), self.assertRaises(ValueError):
                MODULE.require_elapsed(elapsed, 28800)

    def test_untracked_procedure_cannot_claim_clean_git_revision(self):
        with tempfile.TemporaryDirectory() as directory, mock.patch.object(MODULE, 'ROOT', Path(directory)):
            for name in MODULE.PROCEDURE_FILES:
                p = Path(directory) / name; p.parent.mkdir(parents=True, exist_ok=True); p.write_bytes(b'content')
            def git(args, **kwargs):
                if 'rev-parse' in args:
                    return subprocess.CompletedProcess(args, 0, stdout='d' * 40, stderr='')
                if args[-1] == 'HEAD:CI/run_mwnet_overnight.py':
                    return subprocess.CompletedProcess(args, 128, stdout=b'', stderr=b'not tracked')
                return subprocess.CompletedProcess(args, 0, stdout=b'content', stderr=b'')
            with mock.patch.object(MODULE.subprocess, 'run', side_effect=git):
                self.assertTrue(MODULE.procedure_state()[1])

    def test_wrapper_external_watchdog_tracks_last_duration_argument(self):
        with tempfile.TemporaryDirectory(dir=prepare_temp_root()) as directory:
            tmp = Path(directory); tools = tmp / 'tools'; (tools / 'bin').mkdir(parents=True)
            (tools / 'evidence').mkdir(); (tools / 'evidence/compiled-provenance.json').write_text('{}')
            for name in ('bin/mwnet-headless-integration', 'bin/mwnet-directory', 'bin/mwnet-discovery-tests', 'caddy'):
                p = tools / name; p.write_text('#!/bin/sh\nexit 0\n'); p.chmod(0o755)
            fake = tmp / 'docker'
            fake.write_text('#!/usr/bin/env python3\nimport os,sys,json\nopen(os.environ["CAPTURE"],"w").write(json.dumps(sys.argv[1:]))\n')
            fake.chmod(0o755)
            capture = tmp / 'capture.json'
            environment = dict(os.environ, PATH=str(tmp) + ':' + os.environ['PATH'], CAPTURE=str(capture),
                               MWNET_OVERNIGHT_DETACH='1', MWNET_OVERNIGHT_CONTAINER_NAME='test-bounded-watchdog')
            result = subprocess.run(['bash', str(ROOT / 'CI/run_mwnet_overnight_container.sh'), str(tools),
                                     str(tmp / 'new-output'), '--seconds', '30000', '--seconds=28801'],
                                    env=environment, capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            arguments = json.loads(capture.read_text())
            self.assertEqual(arguments[arguments.index('--entrypoint') + 1], '/usr/bin/timeout')
            self.assertIn('29701s', arguments)
            self.assertIn('--kill-after=45s', arguments)
            self.assertIn('--signal=TERM', arguments)
            self.assertIn('--restart=no', arguments)
            self.assertNotIn('--rm', arguments)
            self.assertIn('MWNET_ARTIFACT_SCRATCH=/tmp', arguments)

    def test_scratch_helper_is_part_of_the_frozen_procedure(self):
        self.assertIn('CI/mwnet_disk_tmp.sh', MODULE.PROCEDURE_FILES)
        with tempfile.TemporaryDirectory() as directory, mock.patch.object(MODULE, 'ROOT', Path(directory)):
            for name in MODULE.PROCEDURE_FILES:
                path = Path(directory) / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'content')
            def git(args, **kwargs):
                if 'rev-parse' in args:
                    return subprocess.CompletedProcess(args, 0, stdout='d' * 40, stderr='')
                content = b'changed helper' if args[-1] == 'HEAD:CI/mwnet_disk_tmp.sh' else b'content'
                return subprocess.CompletedProcess(args, 0, stdout=content, stderr=b'')
            with mock.patch.object(MODULE.subprocess, 'run', side_effect=git):
                self.assertTrue(MODULE.procedure_state()[1])

    def test_sibling_scratch_is_included_in_artifact_usage(self):
        with tempfile.TemporaryDirectory(dir=prepare_temp_root()) as directory:
            root, scratch = Path(directory) / 'output', Path(directory) / 'scratch'
            root.mkdir(); scratch.mkdir()
            (root / 'payload').write_bytes(b'abcd')
            (scratch / 'temporary').write_bytes(b'abcdefgh')
            usage = MODULE.artifact_usage(root, scratch)
            self.assertEqual(usage, {'artifactBytes': 12, 'outputBytes': 4, 'temporaryBytes': 8})
            with self.assertRaisesRegex(RuntimeError, 'Artifact/disk headroom'):
                MODULE.artifact_usage(root, scratch, maximum_artifacts_bytes=11)

    def test_nested_scratch_is_not_double_counted_and_links_are_not_followed(self):
        with tempfile.TemporaryDirectory(dir=prepare_temp_root()) as directory:
            root, outside = Path(directory) / 'output', Path(directory) / 'outside'
            root.mkdir(); outside.mkdir()
            scratch = root / 'tmp'; scratch.mkdir()
            (root / 'payload').write_bytes(b'abcd')
            (scratch / 'temporary').write_bytes(b'abcdefgh')
            (outside / 'not-our-data.log').write_bytes(b'outside')
            (scratch / 'outside-link').symlink_to(outside, target_is_directory=True)
            (scratch / 'linked.log').symlink_to(outside / 'not-our-data.log')
            (scratch / 'dangling').symlink_to(outside / 'missing')
            self.assertEqual(MODULE.artifact_usage(root, scratch),
                             {'artifactBytes': 12, 'outputBytes': 12, 'temporaryBytes': 8})

    def test_scratch_logs_and_disk_headroom_have_the_same_bounds(self):
        with tempfile.TemporaryDirectory(dir=prepare_temp_root()) as directory:
            root, scratch = Path(directory) / 'output', Path(directory) / 'scratch'
            root.mkdir(); scratch.mkdir()
            log = scratch / 'temporary.log'; log.write_bytes(b'12345')
            with self.assertRaisesRegex(RuntimeError, 'Bounded log size'):
                MODULE.artifact_usage(root, scratch, maximum_log_bytes=4)
            log.unlink()
            with mock.patch.object(MODULE.shutil, 'disk_usage', return_value=mock.Mock(free=10)):
                with self.assertRaisesRegex(RuntimeError, 'Artifact/disk headroom'):
                    MODULE.artifact_usage(root, scratch, minimum_free_bytes=11)

    def test_scratch_rejects_ram_filesystems_and_nonprivate_directories(self):
        with tempfile.TemporaryDirectory(dir=prepare_temp_root()) as directory:
            root = Path(directory).resolve()
            with mock.patch.dict(MODULE.os.environ, {}, clear=True):
                scratch, filesystem = MODULE.prepare_scratch(root)
                self.assertEqual(scratch, root / 'tmp')
                self.assertNotIn(filesystem, ('tmpfs', 'ramfs'))
                scratch.chmod(0o755)
                with self.assertRaisesRegex(ValueError, 'private permissions'):
                    MODULE.prepare_scratch(root)
                scratch.chmod(0o700)
                with mock.patch.object(MODULE.subprocess, 'run', return_value=mock.Mock(stdout='tmpfs\n')):
                    with self.assertRaisesRegex(ValueError, 'persistent disk'):
                        MODULE.prepare_scratch(root)

    def test_directory_decimal_strings_and_numeric_zero_are_accepted(self):
        metrics = {key: '0' for key in ('listings', 'visible', 'pending', 'challenges', 'rateBuckets', 'listingBytes')}
        self.assertEqual(set(MODULE.directory_cleanup_counts(metrics).values()), {0})
        self.assertEqual(set(MODULE.directory_cleanup_counts({key: 0 for key in metrics}).values()), {0})

    def test_directory_retained_missing_or_invalid_state_fails(self):
        metrics = {key: '0' for key in ('listings', 'visible', 'pending', 'challenges', 'rateBuckets', 'listingBytes')}
        for key in metrics:
            for value in ('1', 1, None, False, '0.1', '-1'):
                with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                    MODULE.directory_cleanup_counts(metrics | {key: value})

    def test_mutated_caddy_is_a_changed_binary(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = {}
            for key in ('native', 'directory', 'discoveryTests', 'caddy'):
                paths[key] = Path(directory) / key
                paths[key].write_bytes(key.encode())
            expected = {key: MODULE.digest(path) for key, path in paths.items()}
            self.assertTrue(MODULE.hashes_unchanged(paths, expected))
            paths['caddy'].write_bytes(b'changed')
            self.assertFalse(MODULE.hashes_unchanged(paths, expected))

    def test_forced_failed_or_lingering_cleanup_cannot_pass(self):
        good = {key: {'exitCode': 0, 'forcedKill': False, 'remainingLiveGroupMembers': []}
                for key in ('native', 'discovery', 'caddy')}
        self.assertTrue(MODULE.cleanup_accepted(good))
        for key in good:
            for change in ({'exitCode': 1}, {'forcedKill': True}, {'remainingLiveGroupMembers': [123]}):
                with self.subTest(component=key, change=change):
                    self.assertFalse(MODULE.cleanup_accepted(good | {key: good[key] | change}))
        self.assertFalse(MODULE.cleanup_accepted({'native': good['native']}))

    def test_real_graceful_process_group_stop(self):
        code = 'import signal,time;signal.signal(signal.SIGTERM,lambda *a:exit(0));print("ready",flush=True);time.sleep(60)'
        process = subprocess.Popen([sys.executable, '-c', code], start_new_session=True,
                                   stdout=subprocess.PIPE, text=True)
        try:
            self.assertTrue(select.select([process.stdout], [], [], 5)[0])
            self.assertEqual(process.stdout.readline().strip(), 'ready')
            result = MODULE.stop_group(process, .3)
            self.assertEqual(result['exitCode'], 0)
            self.assertFalse(result['forcedKill'])
            self.assertFalse(result['remainingLiveGroupMembers'])
        finally:
            if process.poll() is None:
                process.kill(); process.wait()
            process.stdout.close()

    def test_real_unresponsive_group_is_forced_and_rejected(self):
        code = 'import signal,time;signal.signal(signal.SIGTERM,signal.SIG_IGN);print("ready",flush=True);time.sleep(60)'
        process = subprocess.Popen([sys.executable, '-c', code], start_new_session=True,
                                   stdout=subprocess.PIPE, text=True)
        try:
            self.assertTrue(select.select([process.stdout], [], [], 5)[0])
            self.assertEqual(process.stdout.readline().strip(), 'ready')
            result = MODULE.stop_group(process, .1)
            self.assertNotEqual(result['exitCode'], 0)
            self.assertTrue(result['forcedKill'])
            self.assertFalse(result['remainingLiveGroupMembers'])
        finally:
            if process.poll() is None:
                process.kill(); process.wait()
            process.stdout.close()


if __name__ == '__main__':
    unittest.main()
