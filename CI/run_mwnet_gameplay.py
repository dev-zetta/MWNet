#!/usr/bin/env python3
"""Exercise real rendered MWNet clients against an isolated loopback server.

Requires the operator's Morrowind/Tribunal/Bloodmoon data, Xvfb and xauth.
No game assets or credentials are included in the public result receipt.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import math
import os
from pathlib import Path
import re
import secrets
import shutil
import signal
import socket
import subprocess
import sys
import time

from gameplay_content import build_fixture

ROOT = Path(__file__).resolve().parents[1]
FATAL = re.compile(r'\[ERR\]|failed integrity check|multiplayer connection lost|'
                   r'repeated invalid actor|failed to send authenticated keepalive|'
                   r'falling into the void|'
                   r'AddressSanitizer|runtime error:|Segmentation fault')
MAX_LOG = 32 * 1024 * 1024
PROCEDURE_FILES = ('CI/run_mwnet_gameplay.py', 'CI/gameplay_content.py', 'CI/gameplay_fixture.lua')


def procedure_state() -> tuple[str | None, bool | None]:
    """Read the controller revision separately from the tested runtime revision."""
    try:
        revision = subprocess.run(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'],
                                  capture_output=True, text=True, timeout=5)
        dirty = subprocess.run(['git', '-C', str(ROOT), 'diff', '--no-ext-diff', '--quiet',
                                'HEAD', '--', *PROCEDURE_FILES], capture_output=True, timeout=5)
        commit = revision.stdout.strip()
        if revision.returncode or not re.fullmatch('[0-9a-f]{40}', commit) or dirty.returncode not in (0, 1):
            return None, None
        return commit, dirty.returncode == 1
    except (OSError, subprocess.SubprocessError):
        return None, None


def confirmed_incoming_damage(server_text: str) -> dict:
    records = []
    for line in server_text.splitlines():
        marker = 'MWNET_GAMEPLAY_FIXTURE '
        if marker in line:
            records.append(json.loads(line.split(marker, 1)[1]))
    confirmations = [row for row in records if row.get('event') == 'wait_for_hit']
    baselines = [row for row in records if row.get('event') == 'combat']
    if len(confirmations) != 1 or len(baselines) != 1:
        raise RuntimeError('Missing unique canonical incoming damage confirmation')
    confirmed, baseline = confirmations[0], baselines[0]
    health, capacity = float(confirmed['health']), float(confirmed['healthBase'])
    if (not math.isfinite(health) or not math.isfinite(capacity) or not 0 < health < 199
            or capacity != 200 or confirmed['pid'] != baseline['pid']
            or float(baseline['health']) != 200 or float(baseline['healthBase']) != 200):
        raise RuntimeError('Server did not confirm incoming NPC damage at unchanged capacity')
    return confirmed


def validate_server_gameplay(text: str) -> None:
    for line in text.splitlines():
        if (('Rejected ' in line and re.search(r'\(violation [0-9]+\)', line))
                or any(marker in line for marker in ('repeated invalid actor',
                    'failed to send authenticated keepalive', 'Rejected server respawn',
                    'MWNET_GAMEPLAY_FIXTURE_ERROR'))):
            raise RuntimeError('Server rejected ordinary gameplay: ' + line[:300])


def matching_character_stats(expected: dict, observed: dict, *, persisted=False) -> None:
    """Compare native creation's independent oracle with received/saved state."""
    normalize = lambda value: re.sub('[^a-z0-9]', '', value.lower())
    for group, count in (('attributes', 8), ('skills', 27)):
        oracle = {normalize(row['id']): row for row in expected[group]}
        actual = ({normalize(key): value for key, value in observed[group].items()} if persisted
                  else {normalize(row['id']): row for row in observed[group]})
        if len(oracle) != count or set(oracle) != set(actual):
            raise RuntimeError('Character ' + group + ' inventory differs from native creation')
        for name, row in oracle.items():
            for field in (('base', 'damage') if persisted else ('base', 'modifier', 'damage')):
                value, target = float(actual[name][field]), float(row[field])
                if not math.isfinite(value) or not math.isfinite(target) or abs(value - target) > .001:
                    raise RuntimeError(f'Character {group} {name} {field} changed unexpectedly')
    resources = observed['stats'] if persisted else observed
    for key in ('healthBase', 'magickaBase', 'fatigueBase'):
        value, target = float(resources[key]), float(expected[key])
        if not math.isfinite(value) or not math.isfinite(target) or abs(value - target) > .001:
            raise RuntimeError('Character resource capacity changed unexpectedly: ' + key)


def digest(path: Path) -> str:
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def container_resources() -> dict:
    """Read bounded cgroup-v2 metrics only in the explicitly isolated wrapper."""
    if os.environ.get('MWNET_GAMEPLAY_ISOLATED_CONTAINER') != '1':
        return {}
    values = {}
    for filename, key in (('memory.peak', 'peakMemoryBytes'), ('memory.max', 'memoryLimitBytes')):
        try:
            value = Path('/sys/fs/cgroup', filename).read_text()[:64].strip()
            if value.isdecimal():
                values[key] = int(value)
        except OSError:
            pass
    try:
        for line in Path('/sys/fs/cgroup/cpu.stat').read_text()[:4096].splitlines():
            key, value = line.split()
            if key in ('usage_usec', 'user_usec', 'system_usec') and value.isdecimal():
                values[key] = int(value)
    except (OSError, ValueError):
        pass
    return values


def verify_package(root: Path, client: Path, server: Path, version: str, commit: str | None) -> dict:
    """Bind runtime testing to all bytes of the extracted release package."""
    manifest_path = root / 'manifest.json'
    if manifest_path.stat().st_size > 8 * 1024 * 1024:
        raise ValueError('Package manifest exceeds its limit')
    manifest = json.loads(manifest_path.read_text())
    if not isinstance(manifest, dict):
        raise ValueError('Package manifest must be an object')
    files = manifest.get('files')
    if (manifest.get('version') != version or manifest.get('protocol') != 13
            or (commit and manifest.get('runtimeSourceCommit') != commit)
            or not re.fullmatch('[0-9a-f]{40}', str(manifest.get('runtimeSourceCommit', '')))
            or not isinstance(files, dict) or not 1 <= len(files) <= 50_000):
        raise ValueError('Package source/version/file manifest mismatch')
    actual = {p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()}
    if actual != set(files) | {'manifest.json'}:
        raise ValueError('Package file inventory differs from its manifest')
    for name, checksum in files.items():
        path = root / name
        if (len(name) > 4096 or not path.resolve().is_relative_to(root.resolve())
                or not isinstance(checksum, str) or not re.fullmatch('[0-9a-f]{64}', checksum)
                or digest(path) != checksum):
            raise ValueError('Package file checksum mismatch: ' + name)
    if digest(client) != files.get('build/mwnet') or digest(server) != files.get('build/mwnet-server'):
        raise ValueError('Test binaries differ from the release package')
    return {'manifestSha256': digest(manifest_path), 'runtimeSourceCommit': manifest['runtimeSourceCommit'],
            'verifiedFiles': len(files)}


def write_private(path: Path, value: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    with path.open('x', encoding='utf-8') as stream:
        stream.write(value)
    path.chmod(0o600)


def read_events(path: Path) -> list[dict]:
    if not path.exists():
        return []
    if path.stat().st_size > MAX_LOG:
        raise RuntimeError('Gameplay receipt exceeded its disk budget')
    rows = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
    if len(rows) > 4096 or any(not isinstance(row, dict)
                              or not isinstance(row.get('event'), str)
                              or not isinstance(row.get('status'), str) for row in rows):
        raise RuntimeError('Invalid structured gameplay receipt')
    return rows


def validate_result(events: list[dict]) -> None:
    if sum(e.get('event') == 'finish' for e in events) != 1:
        raise RuntimeError('Gameplay receipt requires exactly one terminal record')
    if not events or events[-1].get('event') != 'finish' or events[-1].get('status') != 'pass':
        failure = next((e.get('detail', 'native assertion failed') for e in events
                        if e.get('status') == 'fail'), 'missing terminal native PASS')
        raise RuntimeError(failure)
    if any(e.get('status') == 'fail' for e in events):
        raise RuntimeError('Native gameplay assertions contain a failure')


def validate_rejection(events: list[dict]) -> None:
    if sum(e.get('event') == 'finish' for e in events) != 1:
        raise RuntimeError('Rejected connection requires exactly one terminal record')
    if not events or events[-1].get('event') != 'finish' or events[-1].get('status') != 'fail':
        raise RuntimeError('Missing terminal native rejection')
    if any(e.get('event') == 'finish' and e.get('status') == 'pass' for e in events):
        raise RuntimeError('Rejected connection also reported success')
    if any(e.get('event') in ('snapshot', 'character_stage')
           or (e.get('event') == 'step' and e.get('status') == 'pass') for e in events):
        raise RuntimeError('Rejected connection entered gameplay')


def validate_plan(events: list[dict], actions: list[str]) -> None:
    if not actions or actions[-1] != 'finish':
        raise RuntimeError('Invalid planned gameplay actions')
    steps = [event for event in events if event.get('event') == 'step']
    expected = list(enumerate(actions[:-1]))
    actual = [(event.get('index'), event.get('action')) for event in steps
              if event.get('status') == 'pass']
    if len(steps) != len(expected) or actual != expected:
        raise RuntimeError('Native gameplay receipt omitted, reordered or duplicated a planned assertion')
    terminal = events[-1]
    if terminal.get('index') != len(actions) - 1 or terminal.get('action') != 'finish':
        raise RuntimeError('Native terminal record does not match the planned scenario')


class Child:
    def __init__(self, command: list[str], log: Path, env: dict, cwd: Path):
        self.log = log
        self.output = log.open('wb')
        self.process = subprocess.Popen(command, cwd=cwd, env=env, stdin=subprocess.DEVNULL,
                                        stdout=self.output, stderr=subprocess.STDOUT,
                                        start_new_session=True)

    def stop(self) -> None:
        # Descendants may outlive a parent that exited before our deadline.
        # Their process group still belongs to this explicitly created child.
        try:
            os.killpg(self.process.pid, signal.SIGINT)
        except ProcessLookupError:
            pass
        if self.process.poll() is None:
            try:
                self.process.wait(timeout=8)
            except subprocess.TimeoutExpired:
                pass
        try:
            os.killpg(self.process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        self.process.wait(timeout=5)
        self.output.close()


class GameplayRun:
    def __init__(self, args):
        self.args = args
        self.output = args.output.resolve()
        if self.output.exists():
            raise ValueError('Output must be a new directory; existing evidence is never overwritten')
        self.output.mkdir(parents=True, mode=0o700)
        self.started = time.monotonic()
        self.children: list[Child] = []
        self.cases: list[dict] = []
        self.binary_root = args.client.resolve().parent
        self.package = args.package_root.resolve() if args.package_root else ROOT
        self.client = args.client.resolve()
        self.server_binary = args.server.resolve()
        self.data = args.data.resolve()
        self.fixture = build_fixture(self.data)
        write_private(self.output / 'fixture.json', json.dumps(self.fixture, indent=2) + '\n')
        self.account = 'MWNetAuto' + secrets.token_hex(5)
        self.secret_dir = self.output / 'private'
        self.secret_dir.mkdir(mode=0o700)
        self.account_password = secrets.token_urlsafe(24)
        self.server_password = secrets.token_urlsafe(24)
        write_private(self.secret_dir / 'account-password', self.account_password + '\n')
        write_private(self.secret_dir / 'server-password', self.server_password + '\n')
        self.server_root = self.output / 'server'
        self.server_root.mkdir(mode=0o700)
        self.receipt = {'schemaVersion': 1, 'scope': 'real-rendered-loopback-gameplay',
                        'outcome': 'running', 'version': args.version,
                        'expectedCommit': args.expected_commit, 'protocol': 13,
                        'clientSha256': digest(self.client), 'serverSha256': digest(self.server_binary),
                        'canonicalData': self.fixture['sourceEsm'], 'cases': self.cases,
                        'assetsRedistributed': False, 'manualGameplayRequired': False}
        revision, dirty = procedure_state()
        expected_procedure = getattr(args, 'expected_procedure_revision', None)
        self.receipt.update(procedureRevision=revision, procedureTrackedDirty=dirty,
                            expectedProcedureRevision=expected_procedure)
        self.receipt['procedureSha256'] = {Path(name).name: digest(ROOT / name) for name in PROCEDURE_FILES}
        if expected_procedure and (revision != expected_procedure or dirty is not False):
            raise RuntimeError('Gameplay procedure does not match the clean expected revision')
        if args.package_root:
            self.receipt['package'] = verify_package(self.package, self.client, self.server_binary,
                                                     args.version, args.expected_commit)
        self.env = {key: os.environ[key] for key in ('PATH', 'LD_LIBRARY_PATH', 'OSG_LIBRARY_PATH')
                    if key in os.environ}
        self.env.update(LANG='C.UTF-8', OPENMW_DISABLE_CRASH_CATCHER='1',
                        LIBGL_ALWAYS_SOFTWARE='1', ALSOFT_DRIVERS='null',
                        TMPDIR=str(self.output / 'tmp'))
        (self.output / 'tmp').mkdir(mode=0o700)

    def budget(self) -> None:
        if time.monotonic() - self.started > self.args.timeout:
            raise RuntimeError('Whole gameplay run exceeded its deadline')
        for child in self.children:
            if child.log.exists() and child.log.stat().st_size > MAX_LOG:
                raise RuntimeError('Child log exceeded its disk budget')

    def start(self, command: list[str], log: str, env: dict, cwd: Path) -> Child:
        child = Child(command, self.output / log, env, cwd)
        self.children.append(child)
        return child

    def wait_for(self, predicate, timeout: float, description: str, child: Child | None = None):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.budget()
            result = predicate()
            if result:
                return result
            if child and child.process.poll() is not None:
                raise RuntimeError(f'{description}: process exited; see {child.log.name}')
            time.sleep(.1)
        raise RuntimeError(description + ': deadline exceeded')

    def profile_env(self, profile: Path) -> dict:
        for name in ('config/openmw', 'data/openmw/data', 'cache', 'home'):
            (profile / name).mkdir(parents=True, exist_ok=True, mode=0o700)
        return dict(self.env, HOME=str(profile / 'home'), XDG_CONFIG_HOME=str(profile / 'config'),
                    XDG_DATA_HOME=str(profile / 'data'), XDG_CACHE_HOME=str(profile / 'cache'))

    def start_display(self):
        for program in ('Xvfb', 'xauth') + (('ffmpeg', 'ffprobe') if self.args.record_video else ()):
            if not shutil.which(program):
                raise RuntimeError(f'{program} is required; use Dockerfile.mwnet build environment')
        number = next((n for n in range(90, 1000)
                       if not Path(f'/tmp/.X11-unix/X{n}').exists()), None)
        if number is None:
            raise RuntimeError('No unused test display')
        display = f':{number}'
        auth = self.secret_dir / 'Xauthority'
        auth.touch(mode=0o600)
        cookie = secrets.token_hex(16)
        subprocess.run(['xauth', '-q', '-f', str(auth)],
                       input=f'add {display} MIT-MAGIC-COOKIE-1 {cookie}\n', text=True,
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.env.update(DISPLAY=display, XAUTHORITY=str(auth))
        display_env = dict(self.env, HOME=str(self.output))
        self.display = self.start(['Xvfb', display, '-screen', '0', '960x540x24', '-nolisten',
                                   'tcp', '-noreset', '-auth', str(auth)],
                                  'xvfb.log', display_env, self.output)
        self.wait_for(lambda: Path(f'/tmp/.X11-unix/X{number}').exists(), 10,
                      'Virtual display startup', self.display)

    def prepare_server(self):
        env = self.profile_env(self.server_root)
        env.update(MWNET_TEST_ROOT=str(self.server_root), MWNET_BUILD_DIR=str(self.server_binary.parent),
                   MWNET_CONTENT_DATA_DIR=str(self.data),
                   MWNET_CONTENT_FILES='Morrowind.esm,Tribunal.esm,Bloodmoon.esm',
                   MWNET_GAMEPLAY_TEST='1')
        launcher = self.package / 'run-mwnet-server.sh'
        with (self.output / 'server-prepare.log').open('wb') as log:
            subprocess.run(['bash', str(launcher), '--prepare-only'], env=env, cwd=self.package,
                           check=True, stdout=log, stderr=subprocess.STDOUT, timeout=30)
        # Use the native KDF without placing secrets in argv or environment.
        hashed = subprocess.run([str(self.server_binary), '--hash-access-password'],
                                input=self.server_password + '\n', text=True, capture_output=True,
                                env=env, cwd=self.server_binary.parent, timeout=30)
        if hashed.returncode or not hashed.stdout.strip().startswith('$argon2id$'):
            raise RuntimeError('Native private server password hashing failed')
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.bind(('127.0.0.1', 0))
            self.port = probe.getsockname()[1]
        scripts = self.server_root / 'server/scripts'
        shutil.copyfile(ROOT / 'CI/gameplay_fixture.lua', scripts / 'gameplay_fixture.lua')
        write_private(scripts / 'gameplay-fixture.json', json.dumps(self.fixture) + '\n')
        with (scripts / 'customScripts.lua').open('a') as stream:
            stream.write('\nrequire("gameplay_fixture")\n')
        config = self.server_root / 'xdg-config/openmw/mwnet-server.cfg'
        config.write_text('[General]\nlocalAddress = 127.0.0.1\npublicListen = false\n'
                          f'port = {self.port}\nmaximumPlayers = 4\nhostname = Automated MWNet fixture\n'
                          f'logLevel = 1\npasswordHash = {hashed.stdout.strip()}\n\n'
                          f'[Plugins]\nhome = {self.server_root / "server"}\nplugins = serverCore.lua\n\n'
                          '[Discovery]\nannounce = false\n')
        config.chmod(0o600)
        env.update(XDG_CONFIG_HOME=str(self.server_root / 'xdg-config'),
                   XDG_DATA_HOME=str(self.server_root / 'xdg-data'),
                   XDG_CACHE_HOME=str(self.server_root / 'xdg-cache'))
        self.server = self.start([str(self.server_binary), '--no-logs'], 'server.log', env, self.server_root)
        self.server_pid = self.server.process.pid
        def ready():
            text = self.server.log.read_text(errors='replace')
            match = re.search(r'Server identity fingerprint: (ed25519/[A-Za-z0-9_-]{43})', text)
            if 'Called "OnServerPostInit"' in text and match:
                return match.group(1)
        self.fingerprint = self.wait_for(ready, 45, 'Native fixture startup', self.server)
        text = self.server.log.read_text(errors='replace')
        if self.args.expected_commit and f'Build commit: {self.args.expected_commit[:10]}' not in text:
            raise RuntimeError('Fixture server source revision mismatch')
        if self.args.version and f'dedicated server {self.args.version} ' not in text:
            raise RuntimeError('Fixture server version mismatch')
        self.receipt.update(fingerprint=self.fingerprint, serverPid=self.server_pid)

    def commands(self, verb: str) -> list[dict]:
        return [{'action': 'chat', 'message': '__mwnet_test ' + verb},
                {'action': 'wait_chat', 'text': '__mwnet_test_ack ' + verb},
                {'action': 'wait', 'seconds': .5}]

    def client_case(self, name: str, commands: list[dict], register=False, account=None,
                    expected_failure=False, override=None, content=None, trust_conflict=False,
                    background=False, validation_rejection=False):
        profile = self.output / name
        env = self.profile_env(profile)
        cfg = profile / 'config/openmw'
        selected = content or ['Morrowind.esm', 'Tribunal.esm', 'Bloodmoon.esm']
        resources = next((p for p in (self.client.parent / 'resources', self.package / 'resources',
                                     self.package / 'build/resources')
                          if (p / 'vfs').is_dir() and (p / 'version').is_file()), None)
        if resources is None:
            raise RuntimeError('Missing built/package resources beside the client')
        (cfg / 'openmw.cfg').write_text(f'data="{self.data}"\nresources="{resources}"\n' +
                                      ''.join('content=' + x + '\n' for x in selected) +
                                      ''.join('fallback-archive=' + x + '\n' for x in
                                              ('Morrowind.bsa', 'Tribunal.bsa', 'Bloodmoon.bsa')))
        (cfg / 'settings.cfg').write_text('[Video]\nfullscreen = false\nresolution x = 960\n'
                                        'resolution y = 540\nvsync = false\nframerate limit = 30\n'
                                        '[Camera]\nviewing distance = 2048\n'
                                        '[Shadows]\nenable shadows = false\n')
        if trust_conflict:
            wrong = 'ed25519/' + base64.urlsafe_b64encode(secrets.token_bytes(32)).decode().rstrip('=')
            write_private(cfg / 'trusted-servers.json', json.dumps({'schemaVersion': 1,
                          'servers': {f'127.0.0.1:{self.port}': wrong}}) + '\n')
            conflict_hash = digest(cfg / 'trusted-servers.json')
        result = profile / 'result.jsonl'
        control = {'schemaVersion': 1, 'host': '127.0.0.1', 'port': self.port,
                   'resultFile': str(result), 'timeoutSeconds': 240,
                   'account': account or self.account,
                   'accountPasswordFile': str(self.secret_dir / 'account-password'),
                   'serverPasswordFile': str(self.secret_dir / 'server-password'),
                   'trustFingerprint': self.fingerprint, 'registerAccount': register,
                   'commands': commands}
        if override:
            control.update(override)
        path = profile / 'control.json'
        write_private(path, json.dumps(control) + '\n')
        recorder = None
        if self.args.record_video and not expected_failure and not background:
            video = self.output / f'{name}.mp4'
            recorder = self.start(['ffmpeg', '-nostdin', '-hide_banner', '-loglevel', 'warning',
                                   '-f', 'x11grab', '-framerate', '20', '-video_size', '960x540',
                                   '-i', self.env['DISPLAY'], '-an', '-c:v', 'libx264',
                                   '-preset', 'ultrafast', '-crf', '25', '-pix_fmt', 'yuv420p',
                                   '-movflags', '+faststart', str(video)],
                                  f'{name}-video.log', env, self.output)
        child = self.start([str(self.client), '--gameplay-test-file', str(path), '--no-grab'],
                           f'{name}.log', env, self.client.parent)
        case = {'name': name, 'expectedFailure': bool(expected_failure),
                'expectedErrorPattern': expected_failure if isinstance(expected_failure, str) else None,
                'validationRejection': validation_rejection,
                'receipt': str(result.relative_to(self.output)), 'log': child.log.name, 'status': 'running'}
        case['plannedActions'] = [command['action'] for command in commands]
        case['planSha256'] = hashlib.sha256(json.dumps(commands, sort_keys=True).encode()).hexdigest()
        if trust_conflict:
            case.update(conflictingTrustStore=str((cfg / 'trusted-servers.json').relative_to(self.output)),
                        originalTrustStoreSha256=conflict_hash)
        self.cases.append(case)
        handle = (child, recorder, result, case)
        if background:
            return handle
        return self.finish_case(handle)

    def finish_case(self, handle):
        try:
            return self._finish_case(handle)
        except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
            child, recorder, result, case = handle
            case.update(status='fail', failure=str(error), exitCode=child.process.returncode)
            if recorder:
                recorder.stop()
                video = self.output / f'{case["name"]}.mp4'
                if video.is_file() and video.stat().st_size <= 256 * 1024 * 1024:
                    case.update(video=video.name, videoSha256=digest(video))
            try:
                events = read_events(result)
                terminal = next((e for e in reversed(events) if e.get('event') == 'finish'), None)
                if terminal:
                    case['nativeFailure'] = terminal
            except (OSError, ValueError, RuntimeError):
                pass
            raise

    def _finish_case(self, handle):
        child, recorder, result, case = handle
        self.wait_for(lambda: child.process.poll() is not None,
                      12 if case['validationRejection'] else 260, 'Client scenario ' + case['name'])
        code = child.process.returncode
        if recorder:
            recorder.stop()
        events = read_events(result)
        configured = [e for e in events if e.get('event') == 'configured']
        if not case['validationRejection'] and len(configured) != 1:
            raise RuntimeError('Gameplay receipt requires exactly one native provenance record')
        if configured:
            provenance = configured[0]
            if (provenance.get('version') != self.args.version or provenance.get('protocol') != 13
                    or (self.args.expected_commit
                        and provenance.get('buildCommit') != self.args.expected_commit)):
                raise RuntimeError('Native client gameplay provenance differs from the requested release')
            self.receipt['clientBuildCommit'] = provenance['buildCommit']
        text = child.log.read_text(errors='replace')
        if case['expectedFailure']:
            if case['validationRejection']:
                if code == 0 or any(e.get('status') == 'pass' for e in events):
                    raise RuntimeError(case['name'] + ': unsafe test input was accepted')
            else:
                validate_rejection(events)
            if case['expectedErrorPattern'] and not re.search(case['expectedErrorPattern'], text, re.I):
                raise RuntimeError(case['name'] + ': rejection did not report the expected cause')
        else:
            if code != 0:
                raise RuntimeError(case['name'] + ': client did not exit cleanly')
            validate_result(events)
            validate_plan(events, case['plannedActions'])
            fatal = [line for line in text.splitlines() if FATAL.search(line)]
            if fatal:
                raise RuntimeError(case['name'] + ': client logged a protocol/gameplay failure: ' + fatal[0][:250])
        if self.server.process.poll() is not None or self.server.process.pid != self.server_pid:
            raise RuntimeError('Fixture server exited or changed during client reconnect')
        if case.get('conflictingTrustStore') and digest(self.output / case['conflictingTrustStore']) != case['originalTrustStoreSha256']:
            raise RuntimeError('A failed connection overwrote an existing trusted fingerprint')
        case.update(status='pass', exitCode=code, nativeAssertions=len([e for e in events if e.get('event') == 'step']))
        if recorder:
            video = self.output / f'{case["name"]}.mp4'
            probe = subprocess.run(['ffprobe', '-v', 'error', '-show_entries',
                                    'format=duration:stream=width,height,nb_frames', '-of', 'json', str(video)],
                                   capture_output=True, text=True, check=True, timeout=15)
            metadata = json.loads(probe.stdout)
            if video.stat().st_size > 256 * 1024 * 1024 or float(metadata['format']['duration']) < 1:
                raise RuntimeError('Invalid or oversized gameplay video')
            case.update(video=video.name, videoSha256=digest(video), videoInfo=metadata)
        return events

    def save(self):
        self.receipt['durationSeconds'] = round(time.monotonic() - self.started, 3)
        resources = container_resources()
        if resources:
            self.receipt['containerResources'] = resources
        (self.output / 'gameplay-receipt.json').write_text(json.dumps(self.receipt, indent=2) + '\n')

    def close(self):
        for child in reversed(self.children):
            child.stop()
        self.save()


def run_scenarios(run: GameplayRun):
    fixture = run.fixture
    ready = [{'action': 'wait_ready'}, {'action': 'expect_gameplay_input'}, {'action': 'wait', 'seconds': 1}]
    door = fixture['door']
    container = fixture['container']
    target = fixture['combatTarget']
    dialogue_target = fixture['dialogueTarget']
    target_keys = lambda ref: {'refId': ref['refId'], 'refNum': ref['refNum'], 'mpNum': ref.get('mpNum', 0)}
    seeded = {'action': 'expect_inventory', **target_keys(container), 'itemRefId': 'gold_001', 'count': 7}
    if run.args.scenario == 'dialogue-respawn':
        commands = ready + run.commands('dialogue') + [
            {'action': 'activate', **target_keys(dialogue_target)},
            {'action': 'wait_dialogue', 'present': True},
            {'action': 'snapshot', 'label': 'dialogue_before_death'},
            {'action': 'wait', 'seconds': 1}]
        commands += run.commands('die') + [
            {'action': 'wait_dead', 'timeoutSeconds': 10},
            {'action': 'wait_alive', 'timeoutSeconds': 20},
            {'action': 'wait_dialogue', 'present': False, 'timeoutSeconds': 5},
            {'action': 'expect_gameplay_input'},
            {'action': 'move', 'strafe': 1, 'seconds': 1.5, 'minimumDistance': 5},
            {'action': 'snapshot', 'label': 'after_respawn'}, {'action': 'finish'}]
        run.client_case('dialogue-respawn', commands, register=True)
        return
    phase1 = ready + [{'action': 'snapshot', 'label': 'registered'},
                     {'action': 'move', 'strafe': 1, 'seconds': 1.5, 'minimumDistance': 10},
                     {'action': 'open_inventory'}, {'action': 'wait', 'seconds': .5},
                     {'action': 'close_gui'}]
    phase1 += run.commands('container') + [{'action': 'expect_cell', 'cell': container['cell']}, seeded]
    phase1 += run.commands('door') + [{'action': 'expect_cell', 'cell': door['cell']},
              {'action': 'activate', **target_keys(door)},
              {'action': 'expect_cell', 'cell': door['destination']['cell']},
              {'action': 'wait', 'seconds': 2}, {'action': 'snapshot', 'label': 'door_destination'},
              {'action': 'finish'}]
    registration = run.client_case('registration-movement-door', phase1, register=True)
    local_creation = [e for e in registration if e.get('event') == 'snapshot'
                      and e.get('label') == 'character_created_locally']
    registered = [e for e in registration if e.get('event') == 'snapshot' and e.get('label') == 'registered']
    if len(local_creation) != 1 or len(registered) != 1:
        raise RuntimeError('Missing independent native character creation oracle')
    matching_character_stats(local_creation[0], registered[0])
    run.receipt['characterValidation'] = {'nativeCreationMatchesServer': True}
    run.wait_for(lambda: 'OnPlayerDisconnect' in run.server.log.read_text(errors='replace'),
                 15, 'First client disconnect saved by server', run.server)
    saved_path = run.server_root / 'server/data/player' / (run.account + '.json')
    run.wait_for(saved_path.is_file, 5, 'Canonical character save')
    if saved_path.stat().st_size > 4 * 1024 * 1024:
        raise RuntimeError('Fixture character save exceeded its budget')
    matching_character_stats(local_creation[0], json.loads(saved_path.read_text()), persisted=True)
    run.receipt['characterValidation']['persistedCreationMatchesNative'] = True
    phase2 = ready + [{'action': 'snapshot', 'label': 'reconnected_character'}]
    phase2 += run.commands('container') + [seeded, {'action': 'snapshot', 'label': 'cached_container', **target_keys(container)}]
    phase2 += run.commands('combat') + [
        {'action': 'snapshot', 'label': 'player_before_combat'},
        {'action': 'snapshot', 'label': 'before_combat', **target_keys(target)}]
    phase2 += [{'action': 'attack', **target_keys(target), 'seconds': .8, 'stopOnDialogue': True},
               {'action': 'wait', 'seconds': .5}, {'action': 'resist_arrest', 'timeoutSeconds': 10},
               {'action': 'expect_gameplay_input'}]
    incoming = run.commands('wait_for_hit')
    incoming[1]['timeoutSeconds'] = 35
    phase2 += incoming + [
               {'action': 'expect_actor', 'maximumHealth': 199, 'minimumHealth': 1, 'timeoutSeconds': 10},
               {'action': 'snapshot', 'label': 'player_received_hit'}]
    for hit in range(10):
        phase2 += [{'action': 'attack', **target_keys(target), 'seconds': .5},
                   {'action': 'wait', 'seconds': .5},
                   {'action': 'snapshot', 'label': f'fist-cycle-{hit}', **target_keys(target)}]
    phase2 += [{'action': 'snapshot', 'label': 'after_combat', **target_keys(target)},
               {'action': 'snapshot', 'label': 'player_after_combat'}]
    phase2 += run.commands('dialogue') + [
              {'action': 'activate', **target_keys(dialogue_target)},
              {'action': 'wait_dialogue', 'present': True},
              {'action': 'snapshot', 'label': 'dialogue_before_death'},
              {'action': 'wait', 'seconds': 1}]
    phase2 += run.commands('weakness') + [
              {'action': 'wait_magic_effect', 'effectId': fixture['weakness']['effectRefId'], 'present': True},
              {'action': 'snapshot', 'label': 'active_weakness'}]
    phase2 += run.commands('die_queued') + [{'action': 'wait_dead', 'timeoutSeconds': 10},
              {'action': 'wait_alive', 'timeoutSeconds': 20}, {'action': 'wait', 'seconds': 1},
              {'action': 'wait_dialogue', 'present': False}, {'action': 'expect_gameplay_input'},
              {'action': 'expect_magic_effect', 'effectId': fixture['weakness']['effectRefId'], 'present': False},
              {'action': 'move', 'strafe': 1, 'jump': True, 'seconds': 1.5, 'minimumDistance': 5},
              {'action': 'snapshot', 'label': 'after_respawn'}, {'action': 'finish'}]
    combat_events = run.client_case('fresh-reconnect-combat-respawn', phase2)
    combat = (run.output / 'fresh-reconnect-combat-respawn.log').read_text(errors='replace')
    damages = re.findall(r'Server-confirmed fist impact.*Hand To Hand Hit.*damage ([0-9.]+)', combat)
    if not damages or not any(float(value) > 0 for value in damages):
        raise RuntimeError('Real combat did not produce a successful canonical fist impact')
    cleanups = re.findall(r'(?:Respawn effects cleared|Network effects replaced): queuedRemoved=(\d+) activeRemoved=(\d+)', combat)
    if not cleanups or not any(int(queued) > 0 for queued, _ in cleanups):
        raise RuntimeError('The native queued temporary effect cleanup was not observed')
    if not any(int(active) > 0 for _, active in cleanups):
        raise RuntimeError('The native active temporary effect cleanup was not observed')
    states = {e.get('label'): e for e in combat_events if e.get('event') == 'snapshot'}
    confirmed_hit = confirmed_incoming_damage(run.server.log.read_text(errors='replace'))
    if (states['player_received_hit']['health'] >= states['player_before_combat']['health']
            or states['player_received_hit']['healthBase'] != states['player_before_combat']['healthBase']):
        raise RuntimeError('NPC combat did not damage the player with an unchanged health capacity')
    run.receipt['incomingDamageConfirmation'] = {
        'serverHealth': confirmed_hit['health'], 'serverHealthBase': confirmed_hit['healthBase'],
        'clientHealth': states['player_received_hit']['health'],
        'clientHealthBase': states['player_received_hit']['healthBase'],
        'confirmedBeforePunchLoop': True}
    matching_character_stats(local_creation[0], states['reconnected_character'])
    matching_character_stats(states['dialogue_before_death'], states['after_respawn'])
    remaining = set(states['after_respawn']['activeSpells'])
    if not set(local_creation[0]['activeSpells']).issubset(remaining):
        raise RuntimeError('Respawn removed permanent starting abilities')
    if remaining & {fixture['weakness']['spellId'], 'mwnet_gameplay_queued_weakness'}:
        raise RuntimeError('Respawn retained an active or queued temporary spell')
    run.receipt['characterValidation'].update(reconnectMatchesNative=True, respawnPreservesStats=True)
    if states['after_combat']['health'] >= states['before_combat']['health']:
        raise RuntimeError('Real fist combat did not progress from fatigue damage to health damage')
    if not any(e.get('knockedDown') for label, e in states.items() if label.startswith('fist-cycle-')):
        raise RuntimeError('Real fist combat did not produce an observed knockout')
    # Two independently rendered clients must observe the same live container update.
    primary = ready + run.commands('spawn') + [seeded,
        {'action': 'expect_remote_players', 'minimumCount': 1, 'timeoutSeconds': 60}]
    primary += run.commands('ready')
    primary += run.commands('container_update') + [
        dict(seeded, count=11), {'action': 'snapshot', 'label': 'shared_inventory_sender', **target_keys(container)},
        {'action': 'wait', 'seconds': 5}, {'action': 'finish'}]
    first = run.client_case('two-player-primary', primary, background=True)
    secondary = ready + run.commands('spawn') + [seeded,
        {'action': 'expect_remote_players', 'minimumCount': 1, 'timeoutSeconds': 60}]
    secondary += run.commands('ready') + [
        dict(seeded, count=11, timeoutSeconds=45),
        {'action': 'snapshot', 'label': 'shared_inventory_observer', **target_keys(container)},
        {'action': 'wait', 'seconds': 2}, {'action': 'finish'}]
    run.client_case('two-player-secondary', secondary, register=True, account=run.account + 'B')
    run.finish_case(first)
    for name, override, conflict, content, reason in (
        ('wrong-server-key', {'trustFingerprint': 'ed25519/' + base64.urlsafe_b64encode(secrets.token_bytes(32)).decode().rstrip('=')}, False, None, 'fingerprint|identity'),
        ('saved-key-conflict', None, True, None, 'fingerprint|identity|trust'),
        ('content-mismatch', None, False, ['Morrowind.esm', 'Tribunal.esm'], 'data files|content|manifest'),
    ):
        run.client_case(name, [{'action': 'wait_ready'}, {'action': 'finish'}],
                        expected_failure=reason, override=override, content=content, trust_conflict=conflict)
    for name, override, reason in (
        ('reject-public-control', {'host': '192.0.2.1'}, 'loopback|127.0.0.1'),
        ('reject-unbounded-control', {'timeoutSeconds': 301}, 'bounded|deadline|timeout'),
    ):
        run.client_case(name, [{'action': 'finish'}], expected_failure=reason,
                        override=override, validation_rejection=True)
    fifo = run.secret_dir / 'password-fifo'
    os.mkfifo(fifo, 0o600)
    run.client_case('reject-fifo-credential', [{'action': 'finish'}],
                    override={'accountPasswordFile': str(fifo)}, validation_rejection=True,
                    expected_failure='regular|credential|private|gameplay test')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', required=True, type=Path)
    parser.add_argument('--server', required=True, type=Path)
    parser.add_argument('--data', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--package-root', type=Path)
    parser.add_argument('--expected-commit')
    parser.add_argument('--expected-procedure-revision')
    parser.add_argument('--version', default='1.0.0-alpha.3')
    parser.add_argument('--scenario', choices=('all', 'dialogue-respawn'), default='all')
    parser.add_argument('--timeout', type=int, default=900)
    parser.add_argument('--record-video', action='store_true')
    args = parser.parse_args()
    if not 60 <= args.timeout <= 3600:
        parser.error('Timeout must be between 60 and 3600 seconds')
    if args.expected_commit and not re.fullmatch('[0-9a-f]{40}', args.expected_commit):
        parser.error('Expected commit must be a full Git revision')
    if args.expected_procedure_revision and not re.fullmatch('[0-9a-f]{40}', args.expected_procedure_revision):
        parser.error('Expected procedure revision must be a full Git revision')
    os.umask(0o077)
    run = None
    try:
        run = GameplayRun(args)
        run.start_display()
        run.prepare_server()
        run_scenarios(run)
        server_text = run.server.log.read_text(errors='replace')
        validate_server_gameplay(server_text)
        run.receipt['outcome'] = 'pass'
        print('PASS: autonomous rendered gameplay; receipt: ' + str(run.output / 'gameplay-receipt.json'))
        return 0
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        if run:
            run.receipt.update(outcome='fail', failure=str(error))
        print('Gameplay validation failed: ' + str(error), file=sys.stderr)
        return 1
    finally:
        if run:
            run.close()


if __name__ == '__main__':
    raise SystemExit(main())
