#!/usr/bin/env python3
"""Reversible, host-specific crash diagnostics. Never starts acceptance or flashes firmware."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import pwd
import re
import secrets
import shutil
import signal
import stat
import subprocess
import time

STATE = Path('/var/lib/mwnet-host-diagnostics')
LOGS = Path('/var/log/mwnet-host')
STATUS = Path('/var/lib/mwnet-host-status')
CORES = Path('/var/crash/mwnet-host')
INSTALLED = Path('/usr/local/libexec/mwnet-host-diagnostics')
UNIT = 'mwnet-host-telemetry.service'
SYSCTL = {'kernel.watchdog': '1', 'kernel.nmi_watchdog': '1', 'kernel.hardlockup_panic': '1',
          'kernel.softlockup_panic': '1', 'kernel.panic_on_oops': '1', 'kernel.panic': '30'}
PAYLOADS = ('configure.py', 'telemetry.py', 'mwnet-host-telemetry.service', 'journald.conf', 'lockup.conf')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def command(args, timeout=30, check=True):
    result = subprocess.run(args, capture_output=True, text=True, timeout=timeout,
                            env=dict(os.environ, DEBIAN_FRONTEND='noninteractive', NEEDRESTART_MODE='l'))
    if check and result.returncode:
        raise RuntimeError('Host command failed: ' + args[0])
    return result


def atomic_file(path, data, mode=0o644):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_symlink() or (path.exists() and not path.is_file()):
        raise ValueError('Refusing nonregular configuration target')
    temporary = path.with_name(path.name + '.' + secrets.token_hex(8) + '.mwnet-new')
    try:
        with temporary.open('xb') as stream:
            os.fchmod(stream.fileno(), mode)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        temporary.replace(path)
        descriptor = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)
    finally:
        temporary.unlink(missing_ok=True)


def private_directory(path, gid=None, mode=0o700):
    if path.is_symlink():
        raise ValueError('Refusing symlinked diagnostic directory')
    path.mkdir(mode=mode, parents=True, exist_ok=True)
    if path.stat().st_uid != os.geteuid():
        raise ValueError('Unexpected diagnostic directory owner')
    path.chmod(mode)
    if gid is not None:
        os.chown(path, 0, gid)
    if command(['stat', '-f', '-c', '%T', str(path)]).stdout.strip() in ('tmpfs', 'ramfs'):
        raise ValueError('Diagnostics require disk-backed storage')


def patch_kdump(text):
    replacements = {'KDUMP_COREDIR': '"/var/crash/mwnet-host"', 'KDUMP_NUM_DUMPS': '0',
                    'KDUMP_DUMP_DMESG': '1'}
    lines, seen = [], set()
    for line in text.splitlines():
        match = re.match(r'^\s*(KDUMP_COREDIR|KDUMP_NUM_DUMPS|KDUMP_DUMP_DMESG)\s*=', line)
        if match:
            key = match[1]
            if key in seen:
                raise ValueError('Ambiguous existing kdump configuration')
            seen.add(key)
            line = key + '=' + replacements[key]
        if re.match(r'^\s*(SSH|NFS)\s*=\s*[^#\s]', line):
            raise ValueError('Existing remote dump configuration needs separate review')
        lines.append(line)
    lines += [key + '=' + value for key, value in replacements.items() if key not in seen]
    return '\n'.join(lines) + '\n'


class FileTransaction:
    """Back up bytes and modes before changes; refuse to overwrite later independent edits."""
    def __init__(self, backup):
        self.backup = Path(backup)
        self.entries = []

    def prepare(self, changes):
        self.backup.mkdir(mode=0o700, parents=True, exist_ok=False)
        for index, (path, data, mode) in enumerate(changes):
            if path.is_symlink() or (path.exists() and not path.is_file()):
                raise ValueError('Unsafe configuration target')
            entry = {'path': str(path), 'installedSha256': hashlib.sha256(data).hexdigest(),
                     'existed': path.exists()}
            if path.exists():
                info = path.stat()
                entry.update(mode=stat.S_IMODE(info.st_mode), uid=info.st_uid, gid=info.st_gid,
                             originalSha256=digest(path), backupFile=str(index))
                atomic_file(self.backup / str(index), path.read_bytes(), 0o600)
            self.entries.append(entry)
        atomic_file(self.backup / 'files.json', (json.dumps(self.entries, indent=2) + '\n').encode(), 0o600)

    def check_restorable(self):
        for entry in self.entries:
            path = Path(entry['path'])
            if path.is_symlink():
                raise ValueError('Configuration became a symlink')
            if path.exists() and digest(path) not in (entry['installedSha256'], entry.get('originalSha256')):
                raise ValueError('Preserve independent configuration edits before rollback')
            if entry['existed']:
                saved = self.backup / entry['backupFile']
                if digest(saved) != entry['originalSha256']:
                    raise ValueError('Configuration backup changed')

    def restore(self):
        self.check_restorable()
        for entry in reversed(self.entries):
            path = Path(entry['path'])
            if entry['existed']:
                saved = self.backup / entry['backupFile']
                atomic_file(path, saved.read_bytes(), entry['mode'])
                if os.geteuid() == 0:
                    os.chown(path, entry['uid'], entry['gid'])
            else:
                path.unlink(missing_ok=True)


def manifest_verified(bundle):
    manifest = json.loads((bundle / 'manifest.json').read_text())
    if set(manifest.get('files', {})) != set(PAYLOADS):
        raise ValueError('Unexpected diagnostic bundle inventory')
    for name, expected in manifest['files'].items():
        path = bundle / name
        if path.is_symlink() or digest(path) != expected:
            raise ValueError('Diagnostic bundle changed')
    return manifest


def sysctls():
    return {key: command(['sysctl', '-n', key]).stdout.strip() for key in SYSCTL}


def set_sysctls(values):
    if set(values) != set(SYSCTL) or any(not re.fullmatch('[0-9]+', v) for v in values.values()):
        raise ValueError('Unexpected sysctl restore inventory')
    for key, value in values.items():
        command(['sysctl', '-w', key + '=' + value])


def storage_health():
    result = command(['nvme', 'smart-log', '/dev/nvme0', '-o', 'json'])
    raw = json.loads(result.stdout)
    keys = ('critical_warning', 'temperature', 'available_spare', 'available_spare_threshold',
            'percentage_used', 'power_cycles', 'power_on_hours', 'unsafe_shutdowns', 'media_errors',
            'num_err_log_entries', 'warning_temp_time', 'critical_comp_time')
    selected = {key: raw[key] for key in keys if type(raw.get(key)) in (int, float)}
    selected['inspectionComplete'] = all(key in selected for key in ('critical_warning', 'media_errors', 'unsafe_shutdowns'))
    if not selected['inspectionComplete']:
        raise ValueError('Unexpected NVMe health format')
    selected['healthWarningsPresent'] = selected['critical_warning'] != 0 or selected['media_errors'] != 0
    return selected


def install_nvme_tool(rfkill_required=False):
    required = {'nvme-cli':'nvme'} | ({'rfkill':'rfkill'} if rfkill_required else {})
    missing = [package for package, executable in required.items() if not shutil.which(executable)]
    if not missing:
        return
    plan = command(['apt-get', '-s', '--no-install-recommends', 'install', *missing]).stdout
    mutations = [line for line in plan.splitlines() if line.startswith(('Inst ', 'Remv ', 'Purg '))]
    matches = [re.fullmatch(r'Inst ([a-z0-9-]+) \(([^ ]+) .*', line) for line in mutations]
    if (len(matches) != len(missing) or not all(matches)
            or {match[1] for match in matches} != set(missing)):
        raise ValueError('Diagnostic package plan changed; no package changes applied')
    pinned = [match[1] + '=' + match[2] for match in matches]
    command(['apt-get', '-y', '--no-install-recommends', '--no-upgrade', 'install', *pinned], timeout=300)


def wifi_radio():
    radios = []
    for path in Path('/sys/class/rfkill').glob('rfkill*'):
        if (path / 'type').read_text().strip() == 'wlan':
            radios.append({'id':path.name.removeprefix('rfkill'), 'name':(path / 'name').read_text().strip(),
                           'device':str((path / 'device').resolve()), 'soft':(path / 'soft').read_text().strip()})
    if len(radios) != 1 or '/0000:00:14.3/' not in radios[0]['device']:
        raise ValueError('Unexpected Wi-Fi hardware inventory')
    return radios[0]


def wifi_guard():
    routes = json.loads(command(['ip', '-json', 'route', 'show', 'default']).stdout)
    addresses = json.loads(command(['ip', '-json', 'address', 'show', 'dev', 'wlp0s20f3']).stdout)
    if (len(routes) != 1 or routes[0].get('dev') != 'eno1'
            or Path('/sys/class/net/eno1/carrier').read_text().strip() != '1'
            or len(addresses) != 1 or addresses[0].get('addr_info')):
        raise ValueError('Wi-Fi mitigation requires the active wired route and unused wireless interface')
    return wifi_radio()


def set_wifi(saved, blocked):
    current = wifi_radio()
    if (current['device'], current['name']) != (saved['device'], saved['name']) or blocked not in ('0', '1'):
        raise ValueError('Wi-Fi hardware/state changed')
    command(['systemctl', 'start', 'systemd-rfkill.socket'])
    command(['rfkill', 'block' if blocked == '1' else 'unblock', current['id']])
    path = Path('/var/lib/systemd/rfkill/pci-0000:00:14.3:wlan')
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        if wifi_radio()['soft'] == blocked and path.exists() and path.read_text().strip() == blocked:
            return
        time.sleep(.2)
    raise ValueError('Persistent Wi-Fi state did not match')


def verify():
    manifest_verified(INSTALLED)
    for path in (STATE, STATE / 'tmp', LOGS, CORES):
        if path.is_symlink() or path.stat().st_uid != 0 or stat.S_IMODE(path.stat().st_mode) != 0o700:
            raise ValueError('Diagnostic storage permissions differ')
    if sysctls() != SYSCTL:
        raise ValueError('Lockup capture settings differ')
    if Path('/sys/kernel/kexec_crash_loaded').read_text().strip() != '1':
        raise ValueError('Kernel crash capture is not armed')
    shown = command(['kdump-config', 'show']).stdout
    if 'ready to kdump' not in shown or str(CORES) not in shown:
        raise ValueError('Private dump destination is not ready')
    configuration = command(['systemd-analyze', 'cat-config', 'systemd/journald.conf']).stdout
    expected = {'Storage': 'persistent', 'Compress': 'yes', 'SyncIntervalSec': '5s',
                'SystemMaxUse': '512M', 'SystemKeepFree': '4G', 'RuntimeMaxUse': '32M'}
    merged = {}
    for line in configuration.splitlines():
        if '=' in line and not line.lstrip().startswith(('#', ';')):
            key, value = line.split('=', 1)
            merged[key.strip()] = value.strip()
    if any(merged.get(key) != value for key, value in expected.items()):
        raise ValueError('Persistent journal configuration was overridden')
    marker = 'mwnet-crash-log-probe-' + secrets.token_hex(8)
    command(['logger', '-t', 'mwnet-host-diagnostics', marker])
    command(['journalctl', '--flush'])
    command(['journalctl', '--sync'])
    found = command(['journalctl', '--directory=/var/log/journal', '-t', 'mwnet-host-diagnostics',
                     '--no-pager', '-o', 'cat', '--grep=' + marker, '-n', '1']).stdout
    if marker not in found:
        raise ValueError('Persistent journal probe missing')
    command(['systemctl', 'is-active', '--quiet', UNIT])
    command(['systemctl', 'is-enabled', '--quiet', UNIT])
    limits = dict(line.split('=', 1) for line in command(['systemctl', 'show', UNIT, '-p', 'MemoryMax',
                  '-p', 'TasksMax', '-p', 'NoNewPrivileges', '-p', 'ProtectSystem', '-p', 'Group',
                  '-p', 'CapabilityBoundingSet', '-p', 'PrivateDevices']).stdout.splitlines())
    if limits != {'MemoryMax': '67108864', 'TasksMax': '16', 'NoNewPrivileges': 'yes', 'ProtectSystem': 'strict',
                  'Group': 'gmax', 'CapabilityBoundingSet': '', 'PrivateDevices': 'yes'}:
        raise ValueError('Telemetry service resource/protection settings differ')
    quota, period = (int(value) for value in (Path('/sys/fs/cgroup/system.slice') / UNIT / 'cpu.max').read_text().split())
    if not 0 < quota / period <= .05:
        raise ValueError('Telemetry CPU quota is not bounded')
    deadline, first = time.monotonic() + 16, None
    while time.monotonic() < deadline:
        path = STATUS / 'latest.json'
        if path.exists():
            info = path.stat()
            if info.st_uid != 0 or info.st_gid != 1000 or stat.S_IMODE(info.st_mode) != 0o640:
                raise ValueError('Operator telemetry permissions differ')
            sample = json.loads(path.read_text())
            if sample['bootId'] != Path('/proc/sys/kernel/random/boot_id').read_text().strip():
                raise ValueError('Telemetry is from a different boot')
            stamp = datetime.fromisoformat(sample['utc']).timestamp()
            if time.time() - stamp < 12:
                if first is not None and stamp > first:
                    return {'persistentJournalProbe': True, 'telemetryConsecutiveSamples': True,
                            'telemetryLatestUtc': sample['utc'], 'sysctl': SYSCTL,
                            'crashKernelLoaded': True, 'privateCoreDirectory': str(CORES),
                            'crashKernelReservedBytes': int(Path('/sys/kernel/kexec_crash_size').read_text()),
                            'storage': storage_health(), 'serviceLimits': limits,
                            'panicPathDeliberatelyTriggered': False}
                first = stamp if first is None else first
        time.sleep(.5)
    raise ValueError('Consecutive durable telemetry observations missing')


def refresh_services():
    command(['systemctl', 'daemon-reload'])
    command(['systemctl', 'restart', 'systemd-journald.service'])


def configuration_changes(bundle):
    return ([(INSTALLED / name, (bundle / name).read_bytes(), 0o644) for name in PAYLOADS]
            + [(INSTALLED / 'manifest.json', (bundle / 'manifest.json').read_bytes(), 0o644),
               (Path('/etc/systemd/system') / UNIT, (bundle / UNIT).read_bytes(), 0o644),
               (Path('/etc/systemd/journald.conf.d/90-mwnet-host-diagnostics.conf'), (bundle / 'journald.conf').read_bytes(), 0o644),
               (Path('/etc/sysctl.d/90-mwnet-host-diagnostics.conf'), (bundle / 'lockup.conf').read_bytes(), 0o644),
               (Path('/etc/default/kdump-tools'), patch_kdump(Path('/etc/default/kdump-tools').read_text()).encode(), 0o644)])


def apply(bundle, block_unused_wifi=False):
    manifest = manifest_verified(bundle)
    if Path('/sys/class/dmi/id/product_name').read_text().strip() != 'NUC8i5BEH':
        raise ValueError('This reviewed deployment targets the NUC8i5BEH')
    if Path('/sys/kernel/kexec_crash_loaded').read_text().strip() != '1':
        raise ValueError('Arm the existing crash kernel before enabling lockup panic')
    if shutil.disk_usage('/var').free < 16 * 1024 ** 3:
        raise ValueError('Keep at least16GiB free for diagnostics and crash capture')
    unit = command(['systemctl', 'show', 'mwnet-beta-procfix-20261104.service', '-p', 'ActiveState', '--value']).stdout.strip()
    if unit not in ('inactive', 'failed'):
        raise ValueError('Leave the acceptance test stopped during configuration')
    if command(['docker', '--context', 'default', 'ps', '--filter', 'name=mwnet-beta1-nuc-',
                '--format', '{{.Names}}']).stdout.strip():
        raise ValueError('An acceptance container is still running')
    owner = pwd.getpwnam('gmax')
    if owner.pw_uid != 1000 or owner.pw_gid != 1000:
        raise ValueError('Reviewed operator identity changed')
    radio = wifi_guard() if block_unused_wifi else None
    private_directory(STATE)
    private_directory(STATE / 'tmp')
    os.environ.update(TMPDIR=str(STATE / 'tmp'), TMP=str(STATE / 'tmp'), TEMP=str(STATE / 'tmp'))
    install_nvme_tool(block_unused_wifi)
    backup = STATE / 'backups' / (datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '-' + secrets.token_hex(4))
    changes = configuration_changes(bundle)
    transaction = FileTransaction(backup)
    transaction.prepare(changes)
    previous = {'sysctl': sysctls(), 'wifi': radio, 'telemetryEnabled': command(['systemctl', 'is-enabled', '--quiet', UNIT], check=False).returncode == 0,
                'telemetryActive': command(['systemctl', 'is-active', '--quiet', UNIT], check=False).returncode == 0}
    atomic_file(backup / 'runtime.json', json.dumps(previous, indent=2).encode(), 0o600)
    try:
        private_directory(LOGS)
        private_directory(CORES)
        private_directory(STATUS, owner.pw_gid, 0o750)
        for path, data, mode in changes:
            atomic_file(path, data, mode)
        command(['systemd-analyze', 'verify', '/etc/systemd/system/' + UNIT])
        refresh_services()
        set_sysctls(SYSCTL)
        command(['systemctl', 'enable', '--now', UNIT])
        command(['systemctl', 'restart', UNIT])
        result = verify()
        if radio:
            wifi_guard()
            set_wifi(radio, '1')  # Capture is armed and durable telemetry is running first.
            result['wifiMitigation'] = {'blocked': True, 'previousSoftBlocked': radio['soft'] == '1',
                                        'wiredRouteVerified': True, 'persistentRfkillStateVerified': True,
                                        'causeConfirmed': False}
        result.update(status='configured', backup=str(backup), sourceCommit=manifest['sourceCommit'],
                      testRestarted=False, firmwareFlashed=False, hostRebooted=False)
    except BaseException:
        command(['systemctl', 'stop', UNIT], check=False)
        if not previous['telemetryEnabled']:
            command(['systemctl', 'disable', UNIT], check=False)
        transaction.restore()
        set_sysctls(previous['sysctl'])
        if previous.get('wifi'):
            set_wifi(previous['wifi'], previous['wifi']['soft'])
        refresh_services()
        if previous['telemetryEnabled']:
            command(['systemctl', 'enable', UNIT])
        else:
            command(['systemctl', 'disable', UNIT], check=False)
        if previous['telemetryActive']:
            command(['systemctl', 'start', UNIT])
        raise
    atomic_file(STATE / 'configuration.json', (json.dumps(result, indent=2) + '\n').encode(), 0o600)
    return result


def rollback(backup):
    backup = backup.resolve()
    if not backup.is_relative_to(STATE / 'backups') or backup.stat().st_uid != 0 or backup.stat().st_mode & 0o077:
        raise ValueError('Use the protected backup created by this installer')
    transaction = FileTransaction(backup)
    transaction.entries = json.loads((backup / 'files.json').read_text())
    allowed = {str(INSTALLED / name) for name in (*PAYLOADS, 'manifest.json')}
    allowed |= {'/etc/systemd/system/' + UNIT, '/etc/systemd/journald.conf.d/90-mwnet-host-diagnostics.conf',
                '/etc/sysctl.d/90-mwnet-host-diagnostics.conf', '/etc/default/kdump-tools'}
    if {entry['path'] for entry in transaction.entries} != allowed:
        raise ValueError('Unexpected rollback target inventory')
    previous = json.loads((backup / 'runtime.json').read_text())
    transaction.check_restorable()
    command(['systemctl', 'stop', UNIT], check=False)
    if not previous['telemetryEnabled']:
        command(['systemctl', 'disable', UNIT], check=False)
    transaction.restore()
    set_sysctls(previous['sysctl'])
    if previous.get('wifi'):
        set_wifi(previous['wifi'], previous['wifi']['soft'])
    refresh_services()
    if previous['telemetryEnabled']:
        command(['systemctl', 'enable', UNIT])
    else:
        command(['systemctl', 'disable', UNIT], check=False)
    if previous['telemetryActive']:
        command(['systemctl', 'start', UNIT])
    return {'status': 'rolled-back', 'backup': str(backup), 'logsAndDumpsPreserved': True,
            'diagnosticPackageRetained': True, 'testRestarted': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('apply', 'verify', 'rollback'))
    parser.add_argument('--bundle', type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument('--backup', type=Path)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--block-unused-wifi', action='store_true', help='Guarded, reversible wired-only mitigation')
    args = parser.parse_args()
    if os.geteuid() != 0:
        parser.error('Interactive sudo authentication is required')
    if args.report and args.report.exists():
        parser.error('Preserve the previous report; specify a fresh report filename')
    os.umask(0o077)
    def interrupted(_signum, _frame):
        raise KeyboardInterrupt('Host diagnostic setup interrupted')
    for number in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
        signal.signal(number, interrupted)
    if args.action != 'apply' and (STATE / 'tmp').is_dir():
        os.environ.update(TMPDIR=str(STATE / 'tmp'), TMP=str(STATE / 'tmp'), TEMP=str(STATE / 'tmp'))
    result = {'status': 'failed', 'action': args.action, 'startedUtc': datetime.now(timezone.utc).isoformat()}
    try:
        if args.action == 'apply':
            result.update(apply(args.bundle.resolve(), args.block_unused_wifi))
        elif args.action == 'verify':
            result.update(verify(), status='verified')
        else:
            if args.backup is None:
                parser.error('rollback requires --backup')
            result.update(rollback(args.backup))
    except BaseException as error:
        result.update(errorType=type(error).__name__, failure=str(error))
        raise
    finally:
        result['finishedUtc'] = datetime.now(timezone.utc).isoformat()
        if args.report:
            owner = pwd.getpwnam('gmax')
            parent = args.report.parent
            if parent.is_symlink() or parent.stat().st_uid != owner.pw_uid or parent.stat().st_mode & 0o077:
                raise ValueError('Use a private operator-owned report parent')
            atomic_file(args.report, (json.dumps(result, indent=2) + '\n').encode(), 0o600)
            os.chown(args.report, owner.pw_uid, owner.pw_gid)
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
