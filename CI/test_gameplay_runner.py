#!/usr/bin/env python3
"""Regression tests for autonomous gameplay verdicts and process cleanup.

Uses synthetic receipts and disposable Python process groups, never game data,
public services, account credentials or a graphical session.
"""
from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
SPEC = importlib.util.spec_from_file_location('gameplay_runner', HERE / 'run_mwnet_gameplay.py')
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)


class GameplayRunnerTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def bare_run(self):
        run = object.__new__(RUNNER.GameplayRun)
        run.output = self.root
        run.started = time.monotonic()
        run.args = SimpleNamespace(timeout=60, version='1.0.0-alpha.3', expected_commit='1' * 40)
        run.children = []
        run.receipt = {'schemaVersion': 1, 'outcome': 'running'}
        run.server_pid = 123
        run.server = SimpleNamespace(process=SimpleNamespace(pid=123, poll=lambda: None))
        return run

    def finished_case(self, events, expected_failure=False, with_provenance=True):
        run = self.bare_run()
        if with_provenance:
            events = [dict(e) for e in events]
            configured = next((e for e in events if e.get('event') == 'configured'), None)
            if configured is None:
                configured = {'event': 'configured', 'status': 'pass'}
                events.insert(0, configured)
            configured.update(version=run.args.version, protocol=13, buildCommit=run.args.expected_commit)
        receipt = self.root / 'events.jsonl'
        receipt.write_text(''.join(json.dumps(x) + '\n' for x in events))
        log = self.root / 'client.log'
        log.write_text('fingerprint rejected\n' if expected_failure else '')
        child = SimpleNamespace(process=SimpleNamespace(returncode=0, poll=lambda: 0), log=log)
        case = {'name': 'fixture', 'expectedFailure': expected_failure,
                'validationRejection': False,
                'plannedActions': [e['action'] for e in events if e.get('event') == 'step'] + ['finish'],
                'expectedErrorPattern': 'fingerprint' if expected_failure else None}
        return run, (child, None, receipt, case)

    def test_exit_zero_does_not_override_native_failure(self):
        run, handle = self.finished_case([
            {'event': 'configured', 'status': 'pass'},
            {'event': 'finish', 'status': 'fail', 'detail': 'fixture assertion failed'}])
        with self.assertRaisesRegex(RuntimeError, 'fixture assertion failed'):
            run.finish_case(handle)
        self.assertNotEqual(handle[3].get('status'), 'pass')

    def test_native_provenance_must_identify_one_matching_candidate(self):
        run, handle = self.finished_case([{'event': 'finish', 'status': 'pass'}])
        baseline = RUNNER.read_events(handle[2])
        variants = {'missing': [baseline[-1]],
                    'duplicate': [baseline[0], baseline[0], baseline[-1]]}
        for key, value in [('version', '1.0.0-alpha.2'), ('protocol', 12),
                           ('buildCommit', '2' * 40)]:
            changed = [dict(e) for e in baseline]
            changed[0][key] = value
            variants[key] = changed
        for label, events in variants.items():
            with self.subTest(label=label):
                handle[2].write_text(''.join(json.dumps(e) + '\n' for e in events))
                with self.assertRaises(RuntimeError):
                    run.finish_case(handle)

    def test_terminal_verdict_requires_one_final_pass(self):
        RUNNER.validate_result([{'event': 'finish', 'status': 'pass'}])
        malformed = ([], [{'event': 'step', 'status': 'pass'}],
                     [{'event': 'finish', 'status': 'pass'}, {'event': 'step', 'status': 'pass'}],
                     [{'event': 'step', 'status': 'fail'}, {'event': 'finish', 'status': 'pass'}],
                     [{'event': 'finish', 'status': 'pass'}, {'event': 'finish', 'status': 'pass'}])
        for events in malformed:
            with self.subTest(events=events), self.assertRaises((ValueError, RuntimeError)):
                RUNNER.validate_result(events)

    def test_plan_rejects_missing_reordered_and_duplicate_assertions(self):
        actions = ['wait_ready', 'snapshot', 'finish']
        ready = {'event': 'step', 'status': 'pass', 'index': 0, 'action': 'wait_ready'}
        snapshot = {'event': 'step', 'status': 'pass', 'index': 1, 'action': 'snapshot'}
        terminal = {'event': 'finish', 'status': 'pass', 'index': 2, 'action': 'finish'}
        RUNNER.validate_plan([ready, snapshot, terminal], actions)
        malformed = ([terminal], [ready, terminal], [snapshot, ready, terminal],
                     [ready, snapshot, snapshot, terminal],
                     [ready, dict(snapshot, action='wait'), terminal],
                     [ready, snapshot, dict(terminal, index=1)],
                     [ready, snapshot, dict(terminal, action='snapshot')])
        for events in malformed:
            with self.subTest(events=events), self.assertRaises(RuntimeError):
                RUNNER.validate_plan(events, actions)
        for invalid_plan in ([], ['wait_ready']):
            with self.subTest(actions=invalid_plan), self.assertRaises(RuntimeError):
                RUNNER.validate_plan([ready, snapshot, terminal], invalid_plan)

    def test_case_verdict_checks_every_planned_native_action(self):
        run, handle = self.finished_case([
            {'event': 'step', 'status': 'pass', 'index': 0, 'action': 'wait_ready'},
            {'event': 'finish', 'status': 'pass', 'index': 1, 'action': 'finish'}])
        run.finish_case(handle)
        self.assertEqual(handle[3].pop('status'), 'pass')
        events = [e for e in RUNNER.read_events(handle[2]) if e.get('event') != 'step']
        handle[2].write_text(''.join(json.dumps(e) + '\n' for e in events))
        with self.assertRaisesRegex(RuntimeError, 'planned assertion'):
            run.finish_case(handle)
        self.assertNotEqual(handle[3].get('status'), 'pass')

    def test_void_recovery_cannot_pass_a_gameplay_scenario(self):
        run, handle = self.finished_case([{'event': 'finish', 'status': 'pass', 'index': 0, 'action': 'finish'}])
        handle[0].log.write_text('[WARN] Player position has been reset due to falling into the void\n')
        with self.assertRaisesRegex(RuntimeError, 'gameplay failure'):
            run.finish_case(handle)
        self.assertEqual(handle[3]['status'], 'fail')

    def test_extracted_package_verifies_all_bytes_and_the_actual_test_binaries(self):
        package = self.root / 'package'
        (package / 'build').mkdir(parents=True)
        client = package / 'build/mwnet'
        server = package / 'build/mwnet-server'
        script = package / 'server.lua'
        for path in (client, server, script):
            path.write_text(path.name)
        manifest = {'version': '1.0.0-alpha.3', 'protocol': 13, 'runtimeSourceCommit': '1' * 40,
                    'files': {p.relative_to(package).as_posix(): RUNNER.digest(p)
                              for p in (client, server, script)}}
        manifest_path = package / 'manifest.json'
        manifest_path.write_text(json.dumps(manifest))
        verify = lambda: RUNNER.verify_package(package, client, server, '1.0.0-alpha.3', '1' * 40)
        self.assertEqual(verify()['verifiedFiles'], 3)
        script.write_text('changed CoreScripts')
        with self.assertRaisesRegex(ValueError, 'checksum'):
            verify()
        script.write_text(script.name)
        other = self.root / 'other-client'
        other.write_text('other build')
        with self.assertRaisesRegex(ValueError, 'Test binaries'):
            RUNNER.verify_package(package, other, server, '1.0.0-alpha.3', '1' * 40)
        for field, value in (('version', '1.0.0-alpha.2'), ('protocol', 12),
                             ('runtimeSourceCommit', '2' * 40), ('files', [])):
            manifest_path.write_text(json.dumps(dict(manifest, **{field: value})))
            with self.subTest(field=field), self.assertRaises(ValueError):
                verify()
        manifest_path.write_text(json.dumps(manifest))
        extra = package / 'player-save.json'
        extra.write_text('{}')
        with self.assertRaisesRegex(ValueError, 'inventory'):
            verify()
        extra.unlink()
        script.unlink()
        script.symlink_to(other)
        with self.assertRaises(ValueError):
            verify()

    def test_ordinary_gameplay_rejections_fail_server_acceptance(self):
        for marker in ('Rejected equipment action (violation 1)',
                       'Rejected attribute intent (violation 2)',
                       'Rejected movement (violation 1)', 'Rejected server respawn transition',
                       'MWNET_GAMEPLAY_FIXTURE_ERROR unknown command'):
            with self.subTest(marker=marker), self.assertRaises(RuntimeError):
                RUNNER.validate_server_gameplay('[WARN]: ' + marker)
        RUNNER.validate_server_gameplay('[INFO]: OnPlayerDisconnect\n[INFO]: canonical creation committed')

    def test_incoming_damage_requires_unique_canonical_server_confirmation(self):
        baseline = {'event': 'combat', 'pid': 1, 'health': 200, 'healthBase': 200}
        hit = {'event': 'wait_for_hit', 'pid': 1, 'health': 193, 'healthBase': 200}
        log = lambda rows: '\n'.join('[INFO]: MWNET_GAMEPLAY_FIXTURE ' + json.dumps(row) for row in rows)
        self.assertEqual(RUNNER.confirmed_incoming_damage(log([baseline, hit]))['health'], 193)
        bad_records = ([baseline], [baseline, hit, hit], [hit],
                       [baseline, dict(hit, health=200)], [baseline, dict(hit, health=0)],
                       [baseline, dict(hit, health=float('nan'))],
                       [baseline, dict(hit, healthBase=400)], [baseline, dict(hit, pid=2)],
                       [dict(baseline, health=100), hit])
        for rows in bad_records:
            with self.subTest(rows=rows), self.assertRaises(RuntimeError):
                RUNNER.confirmed_incoming_damage(log(rows))

    def test_native_and_persisted_character_stats_cannot_silently_reset_or_double(self):
        native = {'healthBase': 50, 'magickaBase': 40, 'fatigueBase': 185,
                  'attributes': [{'id': f'attr{i}', 'base': 40 + i, 'modifier': 0, 'damage': 0}
                                 for i in range(8)],
                  'skills': [{'id': f'skill{i}', 'base': 5 + i, 'modifier': 0, 'damage': 0}
                             for i in range(27)]}
        RUNNER.matching_character_stats(native, native)
        for mutation in ('reset', 'double_capacity', 'lost_stat'):
            changed = json.loads(json.dumps(native))
            if mutation == 'reset':
                changed['attributes'][0]['base'] = 1
            elif mutation == 'double_capacity':
                changed['healthBase'] *= 2
            else:
                changed['skills'].pop()
            with self.subTest(mutation=mutation), self.assertRaises(RuntimeError):
                RUNNER.matching_character_stats(native, changed)
        saved = {group: {row['id'].upper(): {'base': row['base'], 'damage': row['damage']}
                         for row in native[group]} for group in ('attributes', 'skills')}
        saved['stats'] = {key: native[key] for key in ('healthBase', 'magickaBase', 'fatigueBase')}
        RUNNER.matching_character_stats(native, saved, persisted=True)
        saved['skills']['SKILL0']['base'] = 1
        with self.assertRaises(RuntimeError):
            RUNNER.matching_character_stats(native, saved, persisted=True)

    def test_expected_rejection_cannot_end_in_pass(self):
        run, handle = self.finished_case([
            {'event': 'finish', 'status': 'fail'},
            {'event': 'finish', 'status': 'pass'}], expected_failure=True)
        with self.assertRaises((ValueError, RuntimeError)):
            run.finish_case(handle)
        self.assertNotEqual(handle[3].get('status'), 'pass')

    def test_connection_rejection_cannot_report_gameplay(self):
        RUNNER.validate_rejection([{'event': 'configured', 'status': 'pass'},
                                   {'event': 'finish', 'status': 'fail'}])
        invalid = ([], [{'event': 'finish', 'status': 'pass'}],
                   [{'event': 'finish', 'status': 'fail'}, {'event': 'finish', 'status': 'fail'}],
                   [{'event': 'snapshot', 'status': 'pass'}, {'event': 'finish', 'status': 'fail'}],
                   [{'event': 'character_stage', 'status': 'pass'}, {'event': 'finish', 'status': 'fail'}],
                   [{'event': 'step', 'status': 'pass', 'action': 'wait_ready'},
                    {'event': 'finish', 'status': 'fail'}])
        for events in invalid:
            with self.subTest(events=events), self.assertRaises((ValueError, RuntimeError)):
                RUNNER.validate_rejection(events)

    def test_configuration_rejection_requires_nonzero_exit_and_matching_reason(self):
        run, handle = self.finished_case([], expected_failure=True, with_provenance=False)
        child, _, _, case = handle
        case['validationRejection'] = True
        with self.assertRaisesRegex(RuntimeError, 'unsafe test input'):
            run.finish_case(handle)
        child.process.returncode = 1
        run.finish_case(handle)
        self.assertEqual(case['status'], 'pass')
        case.pop('status')
        case['expectedErrorPattern'] = 'loopback-only'
        with self.assertRaisesRegex(RuntimeError, 'expected cause'):
            run.finish_case(handle)

    @unittest.skipUnless(os.name == 'posix', 'POSIX private modes and symlink semantics')
    def test_private_files_and_existing_evidence_are_preserved(self):
        target = self.root / 'private/control.json'
        old_umask = os.umask(0)
        try:
            RUNNER.write_private(target, 'fixture-only-secret\n')
        finally:
            os.umask(old_umask)
        self.assertEqual(target.stat().st_mode & 0o777, 0o600)
        self.assertEqual(target.parent.stat().st_mode & 0o777, 0o700)
        with self.assertRaises(FileExistsError):
            RUNNER.write_private(target, 'overwritten')
        self.assertEqual(target.read_text(), 'fixture-only-secret\n')
        alias = self.root / 'alias.json'
        alias.symlink_to(target)
        with self.assertRaises(FileExistsError):
            RUNNER.write_private(alias, 'overwritten')
        self.assertEqual(target.read_text(), 'fixture-only-secret\n')
        with self.assertRaisesRegex(ValueError, 'new directory'):
            RUNNER.GameplayRun(SimpleNamespace(output=self.root))

    def test_receipt_shape_encoding_and_size_fail_closed(self):
        receipt = self.root / 'events.jsonl'
        for value in (b'{bad json}\n', b'[]\n', b'null\n', b'7\n', b'"text"\n', b'\xff\n'):
            receipt.write_bytes(value)
            with self.subTest(value=value), self.assertRaises((ValueError, RuntimeError)):
                RUNNER.read_events(receipt)
        receipt.write_bytes(b' ' * 65)
        with patch.object(RUNNER, 'MAX_LOG', 64), self.assertRaisesRegex(RuntimeError, 'budget'):
            RUNNER.read_events(receipt)

    def child_group(self, parent_exits=False, ignores_interrupt=False):
        marker = self.root / ('orphan.pid' if parent_exits else 'child.pid')
        child_script = ('import signal,time; '
                        + ('signal.signal(signal.SIGINT,signal.SIG_IGN); ' if ignores_interrupt else '')
                        + 'time.sleep(120)')
        script = ('import pathlib,signal,subprocess,sys,time; '
                  + ('signal.signal(signal.SIGINT,signal.SIG_IGN); ' if ignores_interrupt else '')
                  + 'child=subprocess.Popen([sys.executable,"-c",sys.argv[2]]); '
                  + 'pathlib.Path(sys.argv[1]).write_text(str(child.pid)); '
                  + ('sys.exit(0)' if parent_exits else 'time.sleep(120)'))
        child = RUNNER.Child([sys.executable, '-c', script, str(marker), child_script],
                             self.root / (marker.stem + '.log'), {'PATH': os.defpath}, self.root)

        def emergency_cleanup():
            try:
                os.killpg(child.process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            child.process.wait(timeout=5)
            child.output.close()

        self.addCleanup(emergency_cleanup)
        deadline = time.monotonic() + 5
        while not marker.exists() and time.monotonic() < deadline:
            time.sleep(.01)
        self.assertTrue(marker.exists(), 'Disposable child did not start')
        return child, int(marker.read_text())

    @staticmethod
    def alive(pid):
        try:
            status = Path(f'/proc/{pid}/stat')
            if status.exists():
                return status.read_text().split()[2] != 'Z'
            os.kill(pid, 0)
            return True
        except (ProcessLookupError, FileNotFoundError):
            return False

    def assert_group_stopped(self, child, descendant):
        deadline = time.monotonic() + 2
        while self.alive(descendant) and time.monotonic() < deadline:
            time.sleep(.01)
        self.assertIsNotNone(child.process.poll())
        self.assertFalse(self.alive(descendant), 'Gameplay subprocess descendant survived cleanup')
        self.assertTrue(child.output.closed)

    @unittest.skipUnless(os.name == 'posix', 'POSIX process group lifecycle')
    def test_deadline_failure_cleans_entire_child_group(self):
        child, descendant = self.child_group()
        run = self.bare_run()
        run.children = [child]
        run.started = time.monotonic() - 1000
        run.start_display = lambda: run.wait_for(lambda: False, 1, 'Fixture deadline')
        argv = ['run_mwnet_gameplay.py', '--client', '/fixture/client', '--server', '/fixture/server',
                '--data', '/fixture/data', '--output', str(self.root / 'unused')]
        with patch.object(sys, 'argv', argv), patch.object(RUNNER, 'GameplayRun', return_value=run), \
                contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(RUNNER.main(), 1)
        self.assert_group_stopped(child, descendant)
        receipt = json.loads((self.root / 'gameplay-receipt.json').read_text())
        self.assertEqual(receipt['outcome'], 'fail')
        self.assertIn('deadline', receipt['failure'])

    @unittest.skipUnless(os.name == 'posix', 'POSIX process group lifecycle')
    def test_exited_parent_does_not_leave_descendant_running(self):
        child, descendant = self.child_group(parent_exits=True)
        child.process.wait(timeout=5)
        self.assertTrue(self.alive(descendant))
        child.stop()
        self.assert_group_stopped(child, descendant)
        child.stop()  # Cleanup is safe when called again by the driver's finally.

    @unittest.skipUnless(os.name == 'posix', 'POSIX process group lifecycle')
    def test_interrupt_timeout_forces_group_kill(self):
        child, descendant = self.child_group(ignores_interrupt=True)
        original_wait = child.process.wait
        attempts = 0

        def expired_first_wait(timeout):
            nonlocal attempts
            attempts += 1
            if attempts == 1:
                raise subprocess.TimeoutExpired('fixture process group', timeout)
            return original_wait(timeout=timeout)

        with patch.object(child.process, 'wait', side_effect=expired_first_wait):
            child.stop()
        self.assert_group_stopped(child, descendant)
        self.assertEqual(child.process.returncode, -signal.SIGKILL)


if __name__ == '__main__':
    unittest.main()
