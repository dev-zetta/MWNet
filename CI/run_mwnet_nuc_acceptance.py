#!/usr/bin/env python3
"""Run a frozen acceptance bundle independently of the desktop/SSH session.

Requires interactive sudo on the operator's NUC. This prepares a private worker
from an existing local image; it never updates production services or data.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import pwd
import re
import shutil
import signal
import subprocess
import sys
import time
from datetime import datetime, timezone

DOCKER = ['docker', '--context', 'default']


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def verify_inputs(root, manifest):
    if (manifest.get('schemaVersion') != 1
            or not re.fullmatch(r'mwnet-beta1-nuc-[a-z0-9-]{1,40}', manifest.get('runId', ''))
            or any(not re.fullmatch('[0-9a-f]{40}', manifest.get(key, ''))
                   for key in ('runtimeCommit', 'procedureRevision'))
            or not re.fullmatch('[0-9a-f]{64}', manifest.get('compiledProofSha256', ''))):
        raise ValueError('Invalid frozen acceptance identity')
    inputs = manifest.get('files', {})
    images = manifest.get('acceptedBaseImageIds')
    if (not isinstance(images, list) or not 1 <= len(images) <= 3
            or any(not isinstance(image, str) or not re.fullmatch('sha256:[0-9a-f]{64}', image) for image in images)):
        raise ValueError('Invalid accepted base image inventory')
    if not isinstance(inputs, dict) or not 100 <= len(inputs) <= 25000:
        raise ValueError('Invalid frozen input inventory')
    for name, expected in inputs.items():
        relative = Path(name)
        path = root / relative
        if (relative.is_absolute() or '..' in relative.parts or path.is_symlink()
                or not path.resolve().is_relative_to(root.resolve())
                or not re.fullmatch('[0-9a-f]{64}', str(expected)) or digest(path) != expected):
            raise ValueError('Frozen input changed: ' + name)
    # Refuse undeclared code, including symlinks to files or directories.
    actual = set()
    for path in root.rglob('*'):
        if path.is_symlink():
            raise ValueError('Symlink in frozen bundle')
        if path.is_file() and path != root / 'acceptance.json':
            actual.add(str(path.relative_to(root)))
    if actual != set(inputs):
        raise ValueError('Frozen input inventory is incomplete')
    proof = root / 'tools/evidence/compiled-provenance.json'
    if digest(proof) != manifest['compiledProofSha256']:
        raise ValueError('Compiled input proof changed')
    compiled = json.loads(proof.read_text())
    if (compiled['runtimeSourceCommit'] != manifest['runtimeCommit']
            or {key: value['sha256'] for key, value in compiled['binaries'].items()} != manifest['binarySha256']
            or digest(root / 'tools/caddy') != manifest['caddySha256']):
        raise ValueError('Frozen runtime identity differs from compiled proof')


def validate_terminal(result, state, manifest, preflight=False):
    expected = {'status': 'preflight-pass' if preflight else 'passed',
                'preflight': preflight, 'overnightAccepted': not preflight,
                'runtimeSourceCommit': manifest['runtimeCommit'],
                'procedureRevision': manifest['procedureRevision'], 'procedureTrackedDirty': False,
                'compiledInputProofSha256': manifest['compiledProofSha256'],
                'binarySha256': manifest['binarySha256'], 'caddySha256': manifest['caddySha256'],
                'requestedSeconds': 60 if preflight else 28800, 'clients': 16,
                'simulatedLatencyMilliseconds': 75, 'simulatedPacketLossPercent': 2,
                'cleanupAccepted': True, 'binariesUnchanged': True,
                'procedureFilesUnchanged': True, 'hostResourceChecksAccepted': True,
                'finalArtifactBoundsAccepted': True, 'hostGlobalOomKillsDuringRun': 0}
    if any(type(result.get(key)) is not type(value) or result[key] != value for key, value in expected.items()):
        raise ValueError('Incomplete or unsuccessful acceptance result')
    if (state.get('Status') != 'exited' or state.get('Running') is not False
            or state.get('ExitCode') != 0 or state.get('OOMKilled') is not False):
        raise ValueError('Container did not exit successfully without OOM')
    for key in ('elapsedSeconds', 'supervisorWorkloadWallSeconds'):
        value = result.get(key)
        if type(value) not in (int, float) or not expected['requestedSeconds'] <= value < 86400:
            raise ValueError('Workload duration incomplete')
    if result.get('oomEventsDuringRun') != {'oom': 0, 'oom_kill': 0, 'oom_group_kill': 0}:
        raise ValueError('Missing or nonzero OOM observations')
    if result.get('directoryOutagesObserved', 0) < 1 or result.get('directorySuccessfulChurnCycles', 0) < 1:
        raise ValueError('Directory outage/recovery evidence missing')
    if result.get('directoryFinalMetrics') != dict.fromkeys(
            ('listings', 'visible', 'pending', 'challenges', 'rateBuckets', 'listingBytes'), 0):
        raise ValueError('Directory expiry cleanup incomplete')
    if not preflight and any(result.get(key, {}).get('accepted') is not True for key in
                             ('nativeMemoryAssessment', 'directoryMemoryAssessment', 'caddyMemoryAssessment')):
        raise ValueError('Sustained memory acceptance missing')


def command(args, timeout=120, output=None, environment=None):
    completed = subprocess.run(args, stdout=output if output is not None else subprocess.PIPE,
                               stderr=subprocess.STDOUT if output is not None else subprocess.PIPE,
                               timeout=timeout, env=environment)
    if completed.returncode:
        raise RuntimeError('Private worker command failed: ' + ' '.join(map(str, args[:3])))
    return completed.stdout.decode().strip() if output is None else ''


def selected_state(name):
    raw = json.loads(command(DOCKER + ['inspect', name]))[0]
    return {key: raw['State'][key] for key in ('Status', 'Running', 'ExitCode', 'OOMKilled', 'StartedAt', 'FinishedAt')}


def write_status(path, value, owner):
    temporary = path.with_suffix('.new')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.chmod(0o600)
    os.chown(temporary, owner.pw_uid, owner.pw_gid)
    temporary.replace(path)


def run(bundle):
    if os.geteuid() != 0:
        raise ValueError('Start this procedure with the provided sudo systemd-run command')
    os.umask(0o077)
    owner = pwd.getpwnam('gmax')
    if (owner.pw_uid, owner.pw_gid) != (1000, 1000):
        raise ValueError('Reviewed worker UID:GID no longer matches gmax')
    manifest = json.loads((bundle / 'acceptance.json').read_text())
    verify_inputs(bundle, manifest)
    base_ref = 'mwnet-registration:alpha2-4a92d877b4'
    base = json.loads(command(DOCKER + ['image', 'inspect', base_ref]))[0]
    if (base['Id'] not in manifest['acceptedBaseImageIds']
            or base['Architecture'] != 'amd64' or base['Os'] != 'linux'):
        raise ValueError('Existing local worker base differs from the prepared runtime environment')
    root = Path('/srv/mwnet/acceptance') / manifest['runId']
    if command(['stat', '-f', '-c', '%T', '/srv/mwnet']) in ('tmpfs', 'ramfs'):
        raise ValueError('NUC acceptance requires persistent disk')
    if shutil.disk_usage('/srv/mwnet').free < 4 * 1024 ** 3:
        raise ValueError('NUC acceptance needs at least4GiB free disk')
    root.parent.mkdir(mode=0o755, exist_ok=True)
    root.mkdir(mode=0o755)  # Never reuse a previous run, including failed setup.
    receipt = root / 'status.json'
    report = {'schemaVersion': 1, 'status': 'preparing', 'runId': manifest['runId'],
              'scope': 'synthetic components; no production update or sixteen rendered players',
              'runtimeCommit': manifest['runtimeCommit'], 'procedureRevision': manifest['procedureRevision'],
              'startedUtc': datetime.now(timezone.utc).isoformat(), 'baseImageId': base['Id'],
              'productionServicesModified': False, 'container': None}
    write_status(receipt, report, owner)
    active = None
    try:
        frozen = root / 'inputs'
        shutil.copytree(bundle, frozen)
        verify_inputs(frozen, manifest)
        for path in [frozen, *frozen.rglob('*')]:
            path.chmod(0o755 if path.is_dir() or path.stat().st_mode & 0o111 else 0o644)
        artifacts = root / 'artifacts'
        artifacts.mkdir(mode=0o700); os.chown(artifacts, owner.pw_uid, owner.pw_gid)
        scratch = root / 'setup-tmp'; scratch.mkdir(mode=0o700)
        environment = dict(os.environ, TMPDIR=str(scratch), TMP=str(scratch), TEMP=str(scratch),
                           MWNET_DOCKER_CONTEXT='default')
        image = 'mwnet-qa:nuc-' + manifest['procedureRevision'][:12]
        with (root / 'image-build.log').open('x') as stream:
            command(DOCKER + ['build', '--network=none', '--pull=false', '--build-arg',
                              'BASE_IMAGE=' + base_ref, '-t', image, str(frozen / 'image')],
                    timeout=600, output=stream, environment=environment)
        image_id = command(DOCKER + ['image', 'inspect', '--format', '{{.Id}}', image])
        report['workerImageId'] = image_id
        # Exercise all six original CTest commands against the actual worker libs.
        # Source and binaries are read-only; all test output and literal /tmp use disk.
        check = artifacts / 'native-checks'; check.mkdir(mode=0o700)
        os.chown(check, owner.pw_uid, owner.pw_gid)
        with (root / 'native-checks-supervisor.log').open('x') as stream:
            command(DOCKER + ['run', '--rm', '--network', 'none', '--read-only', '--user', '1000:1000',
                              '--memory', '2g', '--memory-swap', '2g', '--cpus', '2', '--pids-limit', '256',
                              '--cap-drop', 'ALL', '--security-opt', 'no-new-privileges',
                              '--mount', f'type=bind,src={check},dst=/tmp',
                              '--mount', f'type=bind,src={frozen / "source"},dst=/source,readonly',
                              '--mount', f'type=bind,src={frozen / "tools"},dst=/tools,readonly',
                              '-e', 'TMPDIR=/tmp', '-e', 'TMP=/tmp', '-e', 'TEMP=/tmp',
                              '--workdir', '/tmp', '--entrypoint', 'python3', image_id,
                              '/source/CI/run_mwnet_worker_checks.py'], timeout=900, output=stream)
        report['nativeChecks'] = json.loads((check / 'checks.json').read_text())
        if report['nativeChecks'].get('passed') != 6:
            raise ValueError('Actual worker native checks incomplete')
        common = dict(environment, MWNET_OVERNIGHT_IMAGE=image_id, MWNET_OVERNIGHT_USER='1000:1000',
                      MWNET_OVERNIGHT_DETACH='1', MWNET_OVERNIGHT_MEMORY='4g', MWNET_OVERNIGHT_CPUS='2')
        for preflight in (True, False):
            phase = 'preflight' if preflight else 'overnight'
            name = manifest['runId'] + ('-preflight' if preflight else '')
            report.update(status=phase, container=name)
            write_status(receipt, report, owner)
            args = ['bash', str(frozen / 'source/CI/run_mwnet_overnight_container.sh'), str(frozen / 'tools'),
                    str(artifacts / phase), '--runtime-commit', manifest['runtimeCommit'],
                    '--procedure-revision', manifest['procedureRevision'], '--clients', '16',
                    '--latency-ms', '75', '--packet-loss-percent', '2', '--seconds', '60' if preflight else '28800']
            if preflight:
                args.append('--preflight')
            active = name
            with (root / (phase + '-launch.log')).open('x') as stream:
                command(args, timeout=120, output=stream,
                        environment=dict(common, MWNET_OVERNIGHT_CONTAINER_NAME=name))
            deadline = time.monotonic() + (1200 if preflight else 30000)
            while True:
                state = selected_state(name)
                report['containerState'] = state
                write_status(receipt, report, owner)
                if not state['Running']:
                    break
                if time.monotonic() >= deadline:
                    raise RuntimeError('External acceptance watchdog expired')
                time.sleep(30)
            active = None
            result_path = artifacts / phase / 'result.json'
            result = json.loads(result_path.read_text())
            validate_terminal(result, state, manifest, preflight)
            for key, relative in [('nativeMetricsSha256', 'native/metrics.json'),
                                  ('discoveryResultSha256', 'discovery/result.json')]:
                if digest(artifacts / phase / relative) != result.get(key):
                    raise ValueError('Terminal component evidence changed')
            report[phase + 'ResultSha256'] = digest(result_path)
            report[phase + 'Accepted'] = not preflight
            report[phase + 'Completed'] = True
        verify_inputs(frozen, manifest)
        report.update(status='passed', overnightAccepted=True, inputsUnchanged=True)
    except BaseException as error:
        if active:
            try:
                command(DOCKER + ['stop', '--time', '45', active], timeout=90)
                report['containerState'] = selected_state(active)
            except (RuntimeError, subprocess.SubprocessError, ValueError) as cleanup_error:
                report['supervisorCleanupFailure'] = str(cleanup_error)
        report.update(status='failed', overnightAccepted=False,
                      failure=str(error) if not isinstance(error, KeyboardInterrupt) else 'Supervisor interrupted')
        raise
    finally:
        report['finishedUtc'] = datetime.now(timezone.utc).isoformat()
        write_status(receipt, report, owner)
    print('PASS: independent NUC acceptance complete; production services unchanged.')


def interrupted(_signal, _frame):
    raise KeyboardInterrupt


if __name__ == '__main__':
    signal.signal(signal.SIGTERM, interrupted)
    try:
        run(Path(sys.argv[1]).resolve())
    except Exception as error:
        print('NUC acceptance stopped: ' + str(error), file=sys.stderr)
        raise SystemExit(1)
