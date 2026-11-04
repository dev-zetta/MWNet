#!/usr/bin/env python3
"""A remote supervisor must never promote partial or mismatched acceptance."""
import copy
import importlib.util
from pathlib import Path
import unittest

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


if __name__ == '__main__':
    unittest.main()
