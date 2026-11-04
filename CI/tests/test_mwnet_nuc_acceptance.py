#!/usr/bin/env python3
"""A remote supervisor must never promote partial or mismatched acceptance."""
import copy
import importlib.util
import hashlib
import json
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location('nuc', Path(__file__).resolve().parents[1] / 'run_mwnet_nuc_acceptance.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class NucVerdictTests(unittest.TestCase):
    def inputs(self, preflight=False):
        manifest = {'runtimeCommit': 'a' * 40, 'procedureRevision': 'b' * 40, 'compiledProofSha256': 'c' * 64,
                    'binarySha256': {'native': 'd' * 64, 'directory': 'e' * 64, 'discoveryTests': 'f' * 64},
                    'caddySha256': '0' * 64}
        state = {'Status': 'exited', 'Running': False, 'ExitCode': 0, 'OOMKilled': False}
        seconds = 60 if preflight else 28800
        result = {'status': 'preflight-pass' if preflight else 'passed', 'preflight': preflight,
                  'overnightAccepted': not preflight, 'runtimeSourceCommit': manifest['runtimeCommit'],
                  'procedureRevision': manifest['procedureRevision'], 'procedureTrackedDirty': False,
                  'compiledInputProofSha256': manifest['compiledProofSha256'], 'requestedSeconds': seconds,
                  'binarySha256': manifest['binarySha256'], 'caddySha256': manifest['caddySha256'],
                  'clients': 16, 'simulatedLatencyMilliseconds': 75, 'simulatedPacketLossPercent': 2,
                  'cleanupAccepted': True, 'binariesUnchanged': True, 'procedureFilesUnchanged': True,
                  'hostResourceChecksAccepted': True, 'finalArtifactBoundsAccepted': True,
                  'hostGlobalOomKillsDuringRun': 0, 'elapsedSeconds': seconds + 10,
                  'supervisorWorkloadWallSeconds': seconds + 1,
                  'oomEventsDuringRun': {'oom': 0, 'oom_kill': 0, 'oom_group_kill': 0},
                  'directoryOutagesObserved': 1, 'directorySuccessfulChurnCycles': 100,
                  'directoryFinalMetrics': dict.fromkeys(('listings', 'visible', 'pending', 'challenges',
                                                        'rateBuckets', 'listingBytes'), 0)}
        for key in ('nativeMemoryAssessment', 'directoryMemoryAssessment', 'caddyMemoryAssessment'):
            result[key] = {'accepted': not preflight}
        return result, state, manifest

    def test_complete_terminal_run_is_accepted(self):
        MODULE.validate_terminal(*self.inputs())

    def test_preflight_never_substitutes_for_full_run(self):
        MODULE.validate_terminal(*self.inputs(True), preflight=True)
        with self.assertRaises(ValueError):
            MODULE.validate_terminal(*self.inputs(True))

    def test_every_missing_receipt_field_is_rejected(self):
        result, state, manifest = self.inputs()
        for key in result:
            damaged = copy.deepcopy(result); del damaged[key]
            with self.subTest(missing=key), self.assertRaises(ValueError):
                MODULE.validate_terminal(damaged, state, manifest)

    def test_failed_short_dirty_oom_and_running_runs_are_rejected(self):
        result, state, manifest = self.inputs()
        changes = [('elapsedSeconds', 28799), ('supervisorWorkloadWallSeconds', 28799),
                   ('runtimeSourceCommit', 'd' * 40), ('procedureRevision', 'd' * 40),
                   ('status', 'running'), ('cleanupAccepted', False), ('binariesUnchanged', False),
                   ('hostGlobalOomKillsDuringRun', 1), ('hostResourceChecksAccepted', False),
                   ('directoryFinalMetrics', {'listings': 1}), ('clients', 2),
                   ('nativeMemoryAssessment', {'accepted': False}), ('elapsedSeconds', float('nan'))]
        for key, value in changes:
            with self.subTest(changed=key), self.assertRaises(ValueError):
                MODULE.validate_terminal(result | {key: value}, state, manifest)
        for key, value in [('Status', 'running'), ('Running', True), ('ExitCode', 1), ('OOMKilled', True)]:
            with self.subTest(state=key), self.assertRaises(ValueError):
                MODULE.validate_terminal(result, state | {key: value}, manifest)

    def test_operator_receipt_excludes_commands_and_private_fixture_fields(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'private-result.json'
            source.write_text(json.dumps({'status': 'running', 'clients': 16,
                                          'processes': {'command': 'private command input'},
                                          'fixtureSecret': 'test-only-placeholder'}))
            observation = MODULE.result_observation(source)
            self.assertEqual(observation['fields'], {'status': 'running', 'clients': 16})
            self.assertEqual(observation['resultSha256'], hashlib.sha256(source.read_bytes()).hexdigest())
            private = root / 'root-private'; private.mkdir(mode=0o700)
            operator = root / 'operator-private'; operator.mkdir(mode=0o700)
            report = {'status': 'overnight', 'overnightObservation': observation}
            MODULE.publish_status(private / 'status.json', operator / 'status.json', report,
                                  SimpleNamespace(pw_uid=os.getuid(), pw_gid=os.getgid()))
            self.assertEqual(json.loads((operator / 'status.json').read_text()), report)
            self.assertEqual((operator / 'status.json').stat().st_mode & 0o777, 0o600)
            self.assertEqual((private / 'status.json').read_bytes(), (operator / 'status.json').read_bytes())

    def test_detached_sequence_exports_status_without_opening_root_artifact_parent(self):
        self.exercise_supervisor(failed_preflight=False)

    def test_failed_preflight_is_retained_and_never_starts_overnight(self):
        self.exercise_supervisor(failed_preflight=True)

    def exercise_supervisor(self, failed_preflight):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            private = root / 'root-private'; private.mkdir(mode=0o700)
            operator = root / 'operator'; operator.mkdir(mode=0o700)
            bundle = operator / 'bundle'; bundle.mkdir()
            result, state, selected = self.inputs()
            manifest = {'schemaVersion': 1, 'runId': 'mwnet-beta1-nuc-fixture',
                        'runtimeCommit': selected['runtimeCommit'], 'procedureRevision': selected['procedureRevision'],
                        'acceptedBaseImageIds': ['sha256:' + '1' * 64], 'binarySha256': selected['binarySha256']}
            for index in range(100):
                path = bundle / 'source' / ('input-' + str(index)); path.parent.mkdir(exist_ok=True)
                path.write_text('fixed-input')
            caddy = bundle / 'tools/caddy'; caddy.parent.mkdir(); caddy.write_bytes(b'fixture-caddy')
            proof = bundle / 'tools/evidence/compiled-provenance.json'; proof.parent.mkdir()
            proof.write_text(json.dumps({'runtimeSourceCommit': manifest['runtimeCommit'],
                                          'binaries': {key: {'sha256': value} for key, value in manifest['binarySha256'].items()}}))
            manifest.update(compiledProofSha256=MODULE.digest(proof), caddySha256=MODULE.digest(caddy))
            manifest['files'] = {str(p.relative_to(bundle)): MODULE.digest(p) for p in bundle.rglob('*') if p.is_file()}
            (bundle / 'acceptance.json').write_text(json.dumps(manifest))
            run_root = private / 'acceptance' / manifest['runId']
            original_path = Path
            def mapped(value):
                path = original_path(value)
                if str(path) == '/srv/mwnet' or str(path).startswith('/srv/mwnet/'):
                    return private / path.relative_to('/srv/mwnet')
                return path
            phases = []
            def command(args, timeout=120, output=None, environment=None):
                if args[0] == 'stat': return 'ext4'
                if args[:5] == MODULE.DOCKER + ['image', 'inspect']:
                    if '--format' in args: return 'sha256:' + '2' * 64
                    return json.dumps([{'Id': manifest['acceptedBaseImageIds'][0], 'Architecture': 'amd64', 'Os': 'linux'}])
                if '/source/CI/run_mwnet_worker_checks.py' in args:
                    (run_root / 'artifacts/native-checks/checks.json').write_text(json.dumps({'passed': 6}))
                if args[0] == 'bash':
                    phase = 'preflight' if '--preflight' in args else 'overnight'; phases.append(phase)
                    self.assertEqual(environment['MWNET_OVERNIGHT_USER'], '1000:1000')
                    output_root = run_root / 'artifacts' / phase; output_root.mkdir()
                    native = output_root / 'native/metrics.json'; native.parent.mkdir(); native.write_text('{}')
                    discovery = output_root / 'discovery/result.json'; discovery.parent.mkdir(); discovery.write_text('{}')
                    receipt, _, _ = self.inputs(phase == 'preflight')
                    receipt.update(compiledInputProofSha256=manifest['compiledProofSha256'],
                                   caddySha256=manifest['caddySha256'],
                                   nativeMetricsSha256=MODULE.digest(native), discoveryResultSha256=MODULE.digest(discovery))
                    if failed_preflight:
                        receipt.update(status='failed', cleanupAccepted=False, failure='fixture startup failure')
                    (output_root / 'result.json').write_text(json.dumps(receipt))
                return ''
            with mock.patch.object(MODULE, 'Path', side_effect=mapped), \
                 mock.patch.object(MODULE.os, 'geteuid', return_value=0), \
                 mock.patch.object(MODULE.pwd, 'getpwnam', return_value=SimpleNamespace(pw_uid=1000, pw_gid=1000)), \
                 mock.patch.object(MODULE.os, 'chown'), \
                 mock.patch.object(MODULE.shutil, 'disk_usage', return_value=SimpleNamespace(free=8 * 1024 ** 3)), \
                 mock.patch.object(MODULE, 'command', side_effect=command), \
                 mock.patch.object(MODULE, 'selected_state', return_value=state), \
                 mock.patch('builtins.print'):
                if failed_preflight:
                    with self.assertRaises(ValueError):
                        MODULE.run(bundle)
                else:
                    MODULE.run(bundle)
            exported = json.loads((operator / (manifest['runId'] + '-status.json')).read_text())
            self.assertEqual(private.stat().st_mode & 0o777, 0o700)
            if failed_preflight:
                self.assertEqual(phases, ['preflight'])
                self.assertEqual(exported['status'], 'failed')
                self.assertFalse(exported['overnightAccepted'])
                self.assertTrue((run_root / 'artifacts/preflight/result.json').is_file())
            else:
                self.assertEqual(phases, ['preflight', 'overnight'])
                self.assertEqual(exported['status'], 'passed')
                self.assertTrue(exported['overnightAccepted'])
                self.assertTrue(exported['inputsUnchanged'])


if __name__ == '__main__':
    unittest.main()
