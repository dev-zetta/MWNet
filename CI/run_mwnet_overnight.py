#!/usr/bin/env python3
"""Bounded lean overnight transport load and local trusted-HTTPS directory outage test.

This is synthetic component acceptance, not sixteen rendered game actors. Legacy
--release-gates semantics and historical results are deliberately untouched.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import signal
import socket
import ssl
import stat
import statistics
import subprocess
import sys
import time
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]
MIB = 1024 ** 2
PROCEDURE_FILES = ('CI/run_mwnet_overnight.py', 'CI/run_mwnet_overnight_container.sh',
                   'CI/run_mwnet_discovery_soak.py', 'CI/mwnet_disk_tmp.sh',
                   'apps/mwnet_tests/headless_integration.cpp')


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def atomic_json(path, value):
    temporary = path.with_suffix('.tmp')
    with temporary.open('w') as stream:
        json.dump(value, stream, indent=2, allow_nan=False)
        stream.write('\n')
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)


def procedure_state():
    commit = subprocess.run(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'],
                            capture_output=True, text=True, check=True, timeout=5).stdout.strip()
    dirty = False
    for name in PROCEDURE_FILES:
        saved = subprocess.run(['git', '-C', str(ROOT), 'show', f'HEAD:{name}'],
                               capture_output=True, timeout=5)
        if saved.returncode or saved.stdout != (ROOT / name).read_bytes():
            dirty = True
    return commit, dirty


def finite_number(value, field):
    if type(value) not in (int, float) or not math.isfinite(value):
        raise ValueError('Missing or nonfinite numeric field: ' + field)
    return value


def natural_integer(value, field):
    if type(value) is not int or value < 0:
        raise ValueError('Missing or invalid integer field: ' + field)
    return value


def require_elapsed(elapsed, requested):
    if finite_number(elapsed, 'supervisor monotonic elapsed') < requested:
        raise ValueError('Supervisor wall time did not complete the requested duration')


def idle_memory_assessment(values, seconds, preflight=False):
    """Use settled lifecycle RSS, excluding short Argon2 KDF allocation peaks."""
    if any(not isinstance(v, (int, float)) or not math.isfinite(v) or v <= 0 for v in values):
        raise ValueError('Invalid native lifecycle RSS samples')
    if preflight:
        return {'scope': 'short preflight; no long-duration memory acceptance', 'accepted': False,
                'samples': len(values)}
    if len(values) < 100:
        raise ValueError('Overnight run needs at least100 lifecycle RSS samples')
    warmup = len(values) // 4
    settled = values[warmup:]
    windows = [statistics.mean(settled[i * len(settled) // 8:(i + 1) * len(settled) // 8])
               for i in range(8)]
    growth = windows[-1] - windows[0]
    window_seconds = seconds * .75 / 8
    slope = statistics.linear_regression([i * window_seconds / 3600 for i in range(8)], windows).slope
    rising = sum(b - a > 64 * 1024 for a, b in zip(windows, windows[1:]))
    # Fixed before launch: a post-warmup step above8MiB needs investigation;
    # sustained growth above4MiB with >=5 rising windows and >0.5MiB/hour fails.
    accepted = growth <= 8 * MIB and not (growth > 4 * MIB and rising >= 5 and slope > .5 * MIB)
    return {'accepted': accepted, 'samples': len(values), 'warmupFraction': .25,
            'windowMeanBytes': windows, 'lastMinusFirstWindowBytes': growth,
            'linearTrendBytesPerHour': slope, 'risingWindowsBeyond64KiB': rising,
            'maximumPostWarmupGrowthBytes': 8 * MIB,
            'sustainedTrendRule': '>4MiB growth, >=5/7 rises over64KiB and >0.5MiB/hour'}


def validate_native(metrics, seconds, clients, latency, loss, commit, preflight):
    expected = {'status': 'passed', 'scenariosComplete': True, 'commit': commit,
                'clients': clients, 'simulatedLatencyMilliseconds': latency,
                'simulatedPacketLossPercent': loss, 'addressSanitizerEnabled': False}
    if any(metrics.get(k) != v for k, v in expected.items()):
        raise ValueError('Native workload/source/scenario identity does not match')
    cycles = natural_integer(metrics.get('cycles'), 'native cycles')
    if cycles < (1 if preflight else 100) or finite_number(metrics.get('elapsedSeconds'), 'native elapsedSeconds') < seconds:
        raise ValueError('Native requested duration/cycles did not finish')
    if natural_integer(metrics.get('maximumQueueDepth'), 'native maximumQueueDepth') > clients:
        raise ValueError('Native observed queue bound exceeded')
    if natural_integer(metrics.get('cleanupVerifiedCycles'), 'native cleanupVerifiedCycles') != cycles:
        raise ValueError('Missing per-cycle ledger/connection cleanup proof')
    for key in ('finalConnectionMetricEntries', 'finalCombatEntries', 'finalInventoryEntries',
                'finalJusticeEntries', 'finalLifecycleEntries'):
        if natural_integer(metrics.get(key), key) != 0:
            raise ValueError('Retained or missing final state: ' + key)
    memory = idle_memory_assessment(metrics.get('residentMemorySamplesBytes', []), seconds, preflight)
    if not preflight and not memory['accepted']:
        raise ValueError('Post-warmup native memory growth needs investigation: ' + json.dumps(memory))
    return memory


def directory_cleanup_counts(metrics):
    result = {}
    for key in ('listings', 'visible', 'pending', 'challenges', 'rateBuckets', 'listingBytes'):
        value = metrics.get(key)
        # Boost.PropertyTree exposes these counters as decimal strings.
        if type(value) is int and value >= 0:
            count = value
        elif isinstance(value, str) and re.fullmatch(r'0|[1-9][0-9]*', value):
            count = int(value)
        else:
            raise ValueError('Missing or invalid directory counter: ' + key)
        if count:
            raise ValueError('Directory retained state after final expiry cleanup: ' + key)
        result[key] = count
    return result


def descendants(pid):
    result, pending = set(), [pid]
    while pending:
        current = pending.pop()
        if current in result:
            continue
        result.add(current)
        if len(result) > 128:
            raise RuntimeError('Unexpected process-tree growth')
        try:
            pending += [int(x) for x in Path(f'/proc/{current}/task/{current}/children').read_text().split()]
        except FileNotFoundError:
            pass
    return result


def observe(pid):
    rss = hwm = fds = 0
    cpu = 0.0
    for child in descendants(pid):
        try:
            fields = dict(line.split(':', 1) for line in Path(f'/proc/{child}/status').read_text().splitlines()
                          if ':' in line)
            rss += int(fields.get('VmRSS', '0').split()[0]) * 1024
            hwm += int(fields.get('VmHWM', '0').split()[0]) * 1024
            stat = Path(f'/proc/{child}/stat').read_text().split(')', 1)[1].split()
            cpu += (int(stat[11]) + int(stat[12])) / os.sysconf('SC_CLK_TCK')
            fds += len(list(Path(f'/proc/{child}/fd').iterdir()))
        except (FileNotFoundError, ProcessLookupError):
            pass
    return {'rssBytes': rss, 'sumProcessPeakRssBytes': hwm, 'cpuSeconds': cpu,
            'openFileDescriptors': fds}


def live_group_members(group):
    members = []
    for path in Path('/proc').iterdir():
        if not path.name.isdecimal():
            continue
        try:
            fields = (path / 'stat').read_text().split(')', 1)[1].split()
            if int(fields[2]) == group and fields[0] != 'Z':
                members.append(int(path.name))
        except (FileNotFoundError, ProcessLookupError, PermissionError):
            pass
    return members


def stop_group(process, grace_seconds=10):
    forced = False
    deadline = time.monotonic() + grace_seconds
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=max(.01, deadline - time.monotonic()))
    except subprocess.TimeoutExpired:
        pass
    while live_group_members(process.pid) and time.monotonic() < deadline:
        time.sleep(.05)
    if live_group_members(process.pid):
        forced = True
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    process.wait(timeout=5)
    return {'exitCode': process.returncode, 'forcedKill': forced,
            'remainingLiveGroupMembers': live_group_members(process.pid)}


def cleanup_accepted(cleanup):
    return (set(cleanup) == {'native', 'discovery', 'caddy'} and
            all(row['exitCode'] == 0 and not row['forcedKill'] and
                not row['remainingLiveGroupMembers'] for row in cleanup.values()))


def hashes_unchanged(paths, expected):
    return set(paths) == set(expected) and all(digest(paths[key]) == value for key, value in expected.items())


def prepare_scratch(root):
    """Honor only the wrapper's explicit private disk mount, never ambient TMPDIR."""
    requested = os.environ.get('MWNET_ARTIFACT_SCRATCH')
    scratch = Path(requested) if requested else root / 'tmp'
    if not scratch.is_absolute() or scratch.is_symlink():
        raise ValueError('Temporary storage must be an absolute private directory')
    scratch.mkdir(mode=0o700, exist_ok=True)
    attributes = scratch.stat()
    if (not stat.S_ISDIR(attributes.st_mode) or attributes.st_uid != os.getuid()
            or stat.S_IMODE(attributes.st_mode) & 0o077):
        raise ValueError('Temporary storage must belong to this user with private permissions')
    scratch = scratch.resolve()
    if scratch == root or scratch in root.parents or attributes.st_dev != root.stat().st_dev:
        raise ValueError('Temporary storage must be separate and on the artifact filesystem')
    filesystem = subprocess.run(['stat', '--file-system', '--format=%T', str(root)],
                                capture_output=True, text=True, check=True, timeout=5).stdout.strip()
    if not filesystem or filesystem in ('tmpfs', 'ramfs'):
        raise ValueError('Artifacts and temporary storage must be on persistent disk')
    return scratch, filesystem


def artifact_usage(root, scratch, maximum_artifacts_bytes=512 * MIB,
                   maximum_log_bytes=32 * MIB, minimum_free_bytes=512 * MIB):
    """Account for sibling scratch too, without following symlinks or special files."""
    def tree_bytes(directory):
        total = 0
        for parent, directories, files in os.walk(directory, followlinks=False):
            directories[:] = [name for name in directories if not (Path(parent) / name).is_symlink()]
            for name in files:
                path = Path(parent) / name
                try:
                    attributes = path.lstat()
                except FileNotFoundError:
                    continue
                if not stat.S_ISREG(attributes.st_mode):
                    continue
                size = attributes.st_size
                if path.suffix == '.log' and size > maximum_log_bytes:
                    raise RuntimeError('Bounded log size exceeded: ' + path.name)
                total += size
        return total
    output_bytes, temporary_bytes = tree_bytes(root), tree_bytes(scratch)
    total = output_bytes + (0 if scratch.is_relative_to(root) else temporary_bytes)
    if total > maximum_artifacts_bytes or shutil.disk_usage(root).free < minimum_free_bytes:
        raise RuntimeError('Artifact/disk headroom bound exceeded')
    return {'artifactBytes': total, 'outputBytes': output_bytes, 'temporaryBytes': temporary_bytes}


def cgroup_constraints():
    result = {}
    for name in ('memory.max', 'memory.swap.max', 'memory.current', 'memory.peak', 'memory.events', 'cpu.stat'):
        try:
            value = (Path('/sys/fs/cgroup') / name).read_text().strip()
            result[name] = ({key: int(count) for key, count in (line.split() for line in value.splitlines())}
                            if name in ('memory.events', 'cpu.stat') else value)
        except FileNotFoundError:
            pass
    return result


def host_memory_observation():
    """Read kernel-wide pressure too: a global OOM can kill outside this cgroup."""
    memory = {}
    for line in Path('/proc/meminfo').read_text().splitlines():
        key, value = line.split(':', 1)
        if key in ('MemTotal', 'MemAvailable', 'SwapTotal', 'SwapFree', 'Shmem'):
            fields = value.split()
            if len(fields) != 2 or fields[1] != 'kB' or not fields[0].isdecimal():
                raise ValueError('Invalid kernel memory observation: ' + key)
            memory[key] = int(fields[0]) * 1024
    counters = dict(line.split() for line in Path('/proc/vmstat').read_text().splitlines())
    if ('MemAvailable' not in memory or 'MemTotal' not in memory
            or not counters.get('oom_kill', '').isdecimal()):
        raise ValueError('Missing kernel memory or global OOM observation')
    return {'memoryBytes': memory, 'globalOomKills': int(counters['oom_kill'])}


def validate_host_memory(observation, baseline, minimum_available_bytes=2048 * MIB):
    available = observation['memoryBytes']['MemAvailable']
    if type(available) is not int or available < minimum_available_bytes:
        raise ValueError('Host memory headroom fell below the declared minimum')
    count, initial = observation['globalOomKills'], baseline['globalOomKills']
    if type(count) is not int or type(initial) is not int or count < initial:
        raise ValueError('Host global OOM counter is invalid or changed generation')
    if count != initial:
        raise ValueError('Host global OOM event interrupted the acceptance environment')


def checked_host_memory(report, baseline=None):
    try:
        observation = host_memory_observation()
        report['lastHostMemoryObservation'] = observation
        validate_host_memory(observation, observation if baseline is None else baseline,
                             report['limits']['minimumHostAvailableBytes'])
        return observation
    except (OSError, ValueError) as error:
        report['hostResourceChecksAccepted'] = False
        report['hostResourceFailure'] = str(error)
        raise


def interrupted(_signal, _frame):
    raise KeyboardInterrupt


def run(args):
    qa_commit, dirty = procedure_state()
    if not args.preflight and (dirty or qa_commit != args.procedure_revision):
        raise ValueError('Overnight acceptance requires the specified clean QA procedure revision')
    proof = json.loads(args.source_proof.read_text())
    if (proof.get('runtimeSourceCommit') != args.runtime_commit
            or proof.get('productionCompiledInputsMatchRuntime') is not True
            or proof.get('qaHarnessSha256') != digest(ROOT / PROCEDURE_FILES[-1])):
        raise ValueError('Compiled input/runtime/harness provenance does not match')
    executables = {'native': args.native, 'directory': args.directory, 'discoveryTests': args.discovery_tests}
    for key, executable in executables.items():
        if proof['binaries'][key]['sha256'] != digest(executable):
            raise ValueError('Changed binary: ' + key)
    if args.output.exists():
        raise ValueError('Use a fresh output directory; old evidence is never overwritten')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if shutil.disk_usage(args.output.parent).free < 2 * 1024 ** 3:
        raise ValueError('Need at least2GiB free persistent disk before launch')
    args.output.mkdir(mode=0o700)
    os.umask(0o077)
    root = args.output.resolve()
    scratch, artifact_filesystem = prepare_scratch(root)
    report = {'schemaVersion': 1, 'scope': 'synthetic transport lifecycle load plus local trusted-HTTPS directory churn/outage',
              'status': 'running', 'preflight': args.preflight, 'overnightAccepted': False,
              'startedUtc': datetime.now(timezone.utc).isoformat(), 'runtimeSourceCommit': args.runtime_commit,
              'procedureRevision': qa_commit, 'procedureTrackedDirty': dirty,
              'procedureSha256': {name: digest(ROOT / name) for name in PROCEDURE_FILES},
              'compiledInputProofSha256': digest(args.source_proof), 'binarySha256': {k: digest(v) for k, v in executables.items()},
              'caddySha256': digest(args.caddy), 'requestedSeconds': args.seconds, 'clients': args.clients,
              'simulatedLatencyMilliseconds': args.latency_ms, 'simulatedPacketLossPercent': args.packet_loss_percent,
              'limits': {'wholeRunDeadlineSeconds': args.seconds + 900, 'maximumArtifactsBytes': 512 * MIB,
                         'maximumIndividualLogBytes': 32 * MIB, 'nativeRssBytes': 1024 * MIB,
                         'discoveryProcessTreeRssBytes': 1024 * MIB, 'caddyRssBytes': 256 * MIB,
                         'minimumHostAvailableBytes': 2048 * MIB},
              'legacyReleaseGateReclassified': False, 'actualGameActors': False,
              'sixteenPlayerRenderedCapacityAccepted': False, 'processes': {},
              'temporaryStorage': {'path': str(scratch), 'filesystem': artifact_filesystem,
                                   'sameFilesystemAsArtifacts': True, 'includedInArtifactBound': True},
              'cgroupConstraintsAtStart': cgroup_constraints(), 'hostResourceChecksAccepted': None}
    atomic_json(root / 'result.json', report)
    processes, streams = {}, []
    started = time.monotonic()
    failure = None
    try:
        report['hostMemoryAtStart'] = checked_host_memory(report)
        atomic_json(root / 'result.json', report)
        tls = root / 'tls'; tls.mkdir(mode=0o700)
        certificate, key = tls / 'localhost.crt', tls / 'localhost.key'
        result = subprocess.run(['openssl', 'req', '-x509', '-newkey', 'ed25519', '-nodes', '-days', '2',
                                 '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost',
                                 '-keyout', str(key), '-out', str(certificate)], capture_output=True, timeout=15)
        if result.returncode:
            raise RuntimeError('Local test-certificate generation failed')
        caddyfile = root / 'Caddyfile'
        caddyfile.write_text('{\n admin off\n auto_https off\n persist_config off\n}\n'
                             'https://localhost:8443 {\n bind 127.0.0.1\n'
                             f' tls "{certificate}" "{key}"\n reverse_proxy 127.0.0.1:8080\n}}\n')
        environment = dict(os.environ, TMPDIR=str(scratch), TMP=str(scratch), TEMP=str(scratch),
                           XDG_DATA_HOME=str(root / 'caddy-data'),
                           XDG_CONFIG_HOME=str(root / 'caddy-config'))
        def launch(name, command):
            stream = (root / (name + '.log')).open('w'); streams.append(stream)
            process = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT,
                                       env=environment, start_new_session=True)
            processes[name] = process
            report['processes'][name] = {'pid': process.pid, 'command': command}
            return process
        launch('caddy', [str(args.caddy), 'run', '--config', str(caddyfile), '--adapter', 'caddyfile'])
        context = ssl.create_default_context(cafile=str(certificate))
        for attempt in range(100):
            if processes['caddy'].poll() is not None:
                raise RuntimeError('Caddy failed to start; inspect private caddy.log')
            try:
                with socket.create_connection(('127.0.0.1', 8443), timeout=.2) as connection:
                    with context.wrap_socket(connection, server_hostname='localhost'):
                        break
            except (OSError, ssl.SSLError):
                time.sleep(.1)
        else:
            raise RuntimeError('Trusted local HTTPS did not become ready')
        native_dir = root / 'native'; native_dir.mkdir(mode=0o700)
        workload_started = time.monotonic()
        launch('native', [str(args.native), '--cycles', '1' if args.preflight else '100',
                         '--clients', str(args.clients), '--duration-seconds', str(args.seconds),
                         '--latency-ms', str(args.latency_ms), '--packet-loss-percent', str(args.packet_loss_percent),
                         '--state-dir', str(native_dir / 'state'), '--metrics-output', str(native_dir / 'metrics.json'),
                         '--commit', args.runtime_commit])
        launch('discovery', [sys.executable, str(ROOT / 'CI/run_mwnet_discovery_soak.py'),
                            '--directory', str(args.directory), '--tests', str(args.discovery_tests),
                            '--origin', 'https://localhost:8443', '--ca', str(certificate),
                            '--artifacts', str(root / 'discovery'), '--revision', args.runtime_commit,
                            '--seconds', str(args.seconds)])
        atomic_json(root / 'result.json', report)
        next_report = time.monotonic()
        with (root / 'resources.jsonl').open('w') as samples:
            while any(processes[name].poll() is None for name in ('native', 'discovery')):
                elapsed = time.monotonic() - started
                if elapsed > args.seconds + 900:
                    raise RuntimeError('Whole-run external deadline exceeded')
                row = {'elapsedSeconds': elapsed,
                       'hostMemory': checked_host_memory(report, report['hostMemoryAtStart'])}
                for name, process in processes.items():
                    if process.poll() is not None:
                        if name == 'caddy' or process.returncode != 0:
                            raise RuntimeError(f'{name} exited unexpectedly: {process.returncode}')
                        continue
                    usage = observe(process.pid); row[name] = usage
                    bound = report['limits'][{'native': 'nativeRssBytes', 'discovery': 'discoveryProcessTreeRssBytes', 'caddy': 'caddyRssBytes'}[name]]
                    if usage['rssBytes'] > bound:
                        raise RuntimeError(name + ' exceeded its predeclared RSS bound')
                row.update(artifact_usage(root, scratch))
                samples.write(json.dumps(row) + '\n'); samples.flush()
                if time.monotonic() >= next_report:
                    report['elapsedSeconds'] = elapsed
                    report['lastResourceSample'] = row
                    report['cgroupConstraintsCurrent'] = cgroup_constraints()
                    atomic_json(root / 'result.json', report)
                    print(f"Overnight {'preflight' if args.preflight else 'validation'} running: {elapsed:.0f}s", flush=True)
                    next_report = time.monotonic() + 60
                time.sleep(2)
        native = json.loads((native_dir / 'metrics.json').read_text())
        report['nativeMemoryAssessment'] = validate_native(native, args.seconds, args.clients, args.latency_ms,
                                                         args.packet_loss_percent, args.runtime_commit, args.preflight)
        report['nativeMetricsSha256'] = digest(native_dir / 'metrics.json')
        report['legacyNativeMemoryDiagnostics'] = {key: native[key] for key in
            ('monotonicMemoryGrowthDetected', 'residentMemoryGrowthPercentAfterWarmup')}
        discovery = json.loads((root / 'discovery/result.json').read_text())
        if (not discovery.get('completed') or discovery.get('errors')
                or discovery.get('workloadExitCode') != 0 or finite_number(discovery.get('elapsedSeconds'), 'directory elapsedSeconds') < args.seconds
                or len(discovery.get('directoryExitCodes', [])) != 2
                or any(code != 0 for code in discovery['directoryExitCodes'])):
            raise ValueError('Directory outage/restart/churn did not finish successfully')
        final = directory_cleanup_counts(discovery.get('finalMetrics', {}))
        workload_text = (root / 'discovery/workload.log').read_text()
        terminal = re.findall(r'HTTPS verification and churn complete; cycles=(\d+) outages=(\d+)', workload_text)
        if len(terminal) != 1 or int(terminal[0][1]) < 1:
            raise ValueError('Missing unique successful churn terminal or observed outage')
        report['discoveryResultSha256'] = digest(root / 'discovery/result.json')
        report['directoryFinalMetrics'] = final
        directory_samples = [json.loads(line) for line in (root / 'discovery/samples.jsonl').read_text().splitlines()]
        database_sizes = [row['databaseBytes'] for row in directory_samples]
        report['directoryDatabaseBytes'] = {'firstSample': database_sizes[0],
                                            'peakSample': max(database_sizes),
                                            'finalSample': database_sizes[-1]}
        directory_rss = [row['directory']['VmRSSBytes'] for row in directory_samples
                         if row.get('directory', {}).get('VmRSSBytes', 0) > 0]
        report['directoryMemoryAssessment'] = idle_memory_assessment(directory_rss, args.seconds, args.preflight)
        resource_samples = [json.loads(line) for line in (root / 'resources.jsonl').read_text().splitlines()]
        caddy_rss = [row['caddy']['rssBytes'] for row in resource_samples
                     if row.get('caddy', {}).get('rssBytes', 0) > 0]
        report['caddyMemoryAssessment'] = idle_memory_assessment(caddy_rss, args.seconds, args.preflight)
        if not args.preflight and any(not report[key]['accepted'] for key in
                                       ('directoryMemoryAssessment', 'caddyMemoryAssessment')):
            raise ValueError('Directory or Caddy sustained memory growth needs investigation')
        report['directoryOutagesObserved'] = int(terminal[0][1])
        report['directorySuccessfulChurnCycles'] = int(terminal[0][0])
        report['supervisorWorkloadWallSeconds'] = time.monotonic() - workload_started
        require_elapsed(report['supervisorWorkloadWallSeconds'], args.seconds)
        report['status'] = 'preflight-pass' if args.preflight else 'passed'
        report['overnightAccepted'] = not args.preflight
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError, KeyboardInterrupt) as error:
        failure = str(error) or 'Interrupted before completion'
        report.update(status='failed', failure=failure, overnightAccepted=False)
    finally:
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        report['cleanup'] = {name: stop_group(process) for name, process in processes.items()}
        report['cleanupAccepted'] = cleanup_accepted(report['cleanup'])
        if not report['cleanupAccepted'] and report['status'] != 'failed':
            report.update(status='failed', failure='Component shutdown was not graceful or left a live owned group',
                          overnightAccepted=False)
        for stream in streams:
            stream.close()
        for name, process in processes.items():
            report['processes'][name]['exitCode'] = process.returncode
            report['processes'][name]['logSha256'] = digest(root / (name + '.log'))
        report['elapsedSeconds'] = time.monotonic() - started
        report['finishedUtc'] = datetime.now(timezone.utc).isoformat()
        report['cgroupConstraintsAtFinish'] = cgroup_constraints()
        before = report['cgroupConstraintsAtStart'].get('memory.events', {})
        after = report['cgroupConstraintsAtFinish'].get('memory.events', {})
        report['oomEventsDuringRun'] = {key: after.get(key, 0) - before.get(key, 0)
                                        for key in ('oom', 'oom_kill', 'oom_group_kill')}
        if any(report['oomEventsDuringRun'].values()):
            report.update(status='failed', failure='Container OOM event observed', overnightAccepted=False)
        try:
            if 'hostMemoryAtStart' not in report:
                raise ValueError('Host initial memory observation was not captured')
            report['hostMemoryAtFinish'] = checked_host_memory(report, report['hostMemoryAtStart'])
            report['hostGlobalOomKillsDuringRun'] = (report['hostMemoryAtFinish']['globalOomKills']
                                                    - report['hostMemoryAtStart']['globalOomKills'])
            if report['hostResourceChecksAccepted'] is not False:
                report['hostResourceChecksAccepted'] = True
        except (OSError, ValueError) as error:
            report['hostResourceChecksAccepted'] = False
            report['hostResourceFailure'] = str(error)
            report.update(status='failed', failure=str(error), overnightAccepted=False)
        report['binariesUnchanged'] = hashes_unchanged(executables | {'caddy': args.caddy},
                                                       report['binarySha256'] | {'caddy': report['caddySha256']})
        if not report['binariesUnchanged']:
            report.update(status='failed', failure='Binary changed during workload', overnightAccepted=False)
        report['procedureFilesUnchanged'] = all(digest(ROOT / k) == v for k, v in report['procedureSha256'].items())
        if not report['procedureFilesUnchanged']:
            report.update(status='failed', failure='Procedure files changed during workload', overnightAccepted=False)
        try:
            report['finalArtifactUsage'] = artifact_usage(root, scratch)
            report['finalArtifactBoundsAccepted'] = True
        except (OSError, RuntimeError) as error:
            report['finalArtifactBoundsAccepted'] = False
            report['artifactBoundsFailure'] = str(error)
            if report['status'] != 'failed':
                report.update(status='failed', failure=str(error), overnightAccepted=False)
        atomic_json(root / 'result.json', report)
    print(json.dumps({'status': report['status'], 'overnightAccepted': report['overnightAccepted'],
                      'receipt': str(root / 'result.json'), 'failure': report.get('failure')}, indent=2))
    return 1 if report['status'] == 'failed' else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('native', 'directory', 'discovery-tests', 'caddy', 'source-proof', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--runtime-commit', required=True)
    parser.add_argument('--procedure-revision')
    parser.add_argument('--seconds', type=int, default=28800)
    parser.add_argument('--clients', type=int, default=16)
    parser.add_argument('--latency-ms', type=int, default=75)
    parser.add_argument('--packet-loss-percent', type=int, default=2)
    parser.add_argument('--preflight', action='store_true')
    args = parser.parse_args()
    if (not re.fullmatch('[0-9a-f]{40}', args.runtime_commit) or not 2 <= args.clients <= 16
            or not 1 <= args.latency_ms <= 1000 or not 1 <= args.packet_loss_percent <= 25
            or (args.preflight and not 20 <= args.seconds <= 600)
            or (not args.preflight and not 28800 <= args.seconds <= 86400)):
        parser.error('Invalid source/workload bounds; overnight minimum is8hours, preflight20–600seconds')
    if not args.preflight and not re.fullmatch('[0-9a-f]{40}', args.procedure_revision or ''):
        parser.error('Overnight acceptance needs an explicit clean QA procedure revision')
    signal.signal(signal.SIGTERM, interrupted)
    try:
        return run(args)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print('Cannot start overnight validation: ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
