#!/usr/bin/env python3
"""Install/check/uninstall a hash-pinned candidate on disposable Windows CI only."""
from __future__ import annotations

import argparse
import ctypes
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys

from inspect_mwnet_windows_dependencies import extract_checked, sha
from mwnet_temp import configure_temp_environment


REDISTRIBUTABLE = 'redist/vc_redist.x64.exe'


def inventory(root):
    rows = {}
    for path in sorted(root.rglob('*')):
        if path.is_symlink():
            raise ValueError('Unexpected installed/portable payload symlink')
        if path.is_file():
            relative = path.relative_to(root).as_posix()
            key = relative.casefold()
            if key in rows:
                raise ValueError('Case-aliased payload files')
            rows[key] = {'path': relative, 'sha256': sha(path), 'bytes': path.stat().st_size}
    return rows


def compare_installed(portable, installed):
    expected, actual = inventory(portable), inventory(installed)
    if REDISTRIBUTABLE not in expected:
        raise ValueError('Expected the original portable VC runtime redistributable')
    # The frozen runtime CPack install hook runs this executable and removes
    # its redist directory. The other portable bytes must remain identical.
    expected.pop(REDISTRIBUTABLE)
    missing = sorted(set(expected) - set(actual))
    changed = sorted(key for key in set(expected) & set(actual)
                     if expected[key]['sha256'] != actual[key]['sha256'])
    extra = sorted(set(actual) - set(expected))
    if missing or changed or extra != ['uninstall.exe']:
        raise ValueError(json.dumps({'missing': missing, 'changed': changed,
                                     'extra': extra}, sort_keys=True))
    return {'portableFiles': len(expected) + 1, 'byteIdenticalInstalledPayloadFiles': len(expected),
            'portableOnlyRedistributableRemovedByInstallHook': REDISTRIBUTABLE,
            'generatedUninstaller': actual['uninstall.exe'], 'installedFiles': list(actual.values())}


def registered_mwnet_installs():
    import winreg
    matches = []
    for hive, label in [(winreg.HKEY_LOCAL_MACHINE, 'HKLM'), (winreg.HKEY_CURRENT_USER, 'HKCU')]:
        for view in [winreg.KEY_WOW64_64KEY, winreg.KEY_WOW64_32KEY]:
            try:
                key = winreg.OpenKey(hive, r'Software\Microsoft\Windows\CurrentVersion\Uninstall',
                                     0, winreg.KEY_READ | view)
            except FileNotFoundError:
                continue
            with key:
                for index in range(2048):
                    try:
                        child = winreg.EnumKey(key, index)
                    except OSError as error:
                        if error.winerror == 259:  # ERROR_NO_MORE_ITEMS
                            break
                        raise
                    with winreg.OpenKey(key, child) as entry:
                        try:
                            display, _ = winreg.QueryValueEx(entry, 'DisplayName')
                        except FileNotFoundError:
                            continue
                        if isinstance(display, str) and display.casefold().startswith('mwnet'):
                            matches.append({'hive': label, 'view': view, 'key': child, 'displayName': display})
                else:
                    raise ValueError('Uninstall registry inventory exceeded its bound')
    return matches


def run_logged(command, log, timeout):
    with log.open('x', encoding='utf-8') as stream:
        process = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT)
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            # This PID belongs to this disposable test. No image-name or
            # machine-wide process cleanup is used.
            subprocess.run(['taskkill.exe', '/PID', str(process.pid), '/T', '/F'],
                           stdout=stream, stderr=subprocess.STDOUT, timeout=30, check=False)
            process.wait(timeout=30)
            raise
    if code != 0:
        raise ValueError('Native test command returned ' + str(code) + '; see ' + log.name)
    return code


def validate_destination(workspace, output):
    workspace, output = workspace.resolve(strict=True), output.resolve()
    if not output.is_relative_to(workspace) or output == workspace:
        raise ValueError('Installer evidence/destination must be inside the disposable workspace')
    if any(character.isspace() for character in str(output)) or '"' in str(output):
        raise ValueError('Use an unquoted, whitespace-free NSIS test destination')
    if output.exists():
        raise ValueError('Refusing to overwrite installer evidence or an existing destination')
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--installer', type=Path, required=True)
    parser.add_argument('--installer-sha256', required=True)
    parser.add_argument('--portable', type=Path, required=True)
    parser.add_argument('--portable-sha256', required=True)
    parser.add_argument('--runtime', required=True)
    parser.add_argument('--source-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if (sys.platform != 'win32' or os.environ.get('GITHUB_ACTIONS') != 'true'
            or os.environ.get('RUNNER_OS') != 'Windows'):
        parser.error('Installer execution is restricted to disposable native Windows GitHub Actions')
    configure_temp_environment()
    output = validate_destination(Path(os.environ['GITHUB_WORKSPACE']), args.output)
    for path, expected in [(args.installer, args.installer_sha256), (args.portable, args.portable_sha256)]:
        if not re.fullmatch('[0-9a-f]{64}', expected) or sha(path) != expected:
            parser.error('Original candidate hash mismatch: ' + path.name)
    actual = subprocess.check_output(['git', '-C', str(args.source_dir), 'rev-parse', 'HEAD'], text=True).strip()
    if not re.fullmatch('[0-9a-f]{40}', args.runtime) or actual != args.runtime:
        parser.error('Runtime checkout does not match the frozen candidate')
    output.mkdir(parents=True)
    report = {'status': 'running', 'runtimeSourceCommit': args.runtime,
              'installerSha256': args.installer_sha256, 'portableSha256': args.portable_sha256,
              'toolSha256': sha(Path(__file__)),
              'host': platform.platform(), 'preprovisionedDisposableRunner': True,
              'scope': 'Fresh silent installation, actual installed payload/startup and uninstall on disposable Windows CI; not upgrade, graphical gameplay or distribution acceptance',
              'installationAccepted': False, 'installedStartupAccepted': False,
              'uninstallationAccepted': False, 'graphicalGameplayAccepted': False,
              'distributionAccepted': False, 'vcRuntimeBootstrapOnCleanOsAccepted': False}
    try:
        if not ctypes.windll.shell32.IsUserAnAdmin():
            raise ValueError('Disposable runner lacks the installer admin token; no interactive elevation attempted')
        previous = registered_mwnet_installs()
        if previous:
            raise ValueError('Pre-existing MWNet install found; refusing to change it')
        report['noPreviousMwnetInstallation'] = True
        portable = extract_checked(args.portable, args.portable_sha256, output / 'portable')
        installed = output / 'installed'
        report['installExitCode'] = run_logged([str(args.installer.resolve()), '/S', '/D=' + str(installed)],
                                              output / 'installer.log', 300)
        report['actualInstalledFilesBeforeComparison'] = list(inventory(installed).values())
        report['payloadComparison'] = compare_installed(portable, installed)
        report['registeredAfterInstall'] = registered_mwnet_installs()
        if not report['registeredAfterInstall']:
            raise ValueError('No MWNet uninstall registration was created')
        report['installationAccepted'] = True
        smoke = Path(__file__).with_name('smoke_mwnet_package.py')
        report['startupExitCode'] = run_logged([sys.executable, str(smoke.resolve()), str(installed),
                                               '--commit', args.runtime, '--source-dir', str(args.source_dir.resolve())],
                                              output / 'installed-startup.log', 90)
        compare_installed(portable, installed)
        report['installedStartupAccepted'] = True
        uninstaller = installed / report['payloadComparison']['generatedUninstaller']['path']
        report['uninstallExitCode'] = run_logged([str(uninstaller), '/S', '_?=' + str(installed)],
                                                output / 'uninstaller.log', 180)
        remaining = inventory(installed)
        if set(remaining) - {'uninstall.exe'}:
            raise ValueError('Uninstaller left payload files: ' + ', '.join(sorted(remaining)))
        report['remainingUninstallerFiles'] = list(remaining.values())
        report['registeredAfterUninstall'] = registered_mwnet_installs()
        if report['registeredAfterUninstall']:
            raise ValueError('MWNet uninstall registration remains')
        report['uninstallationAccepted'] = True
        report['status'] = 'passed'
    except Exception as error:
        report['status'] = 'failed'
        report['error'] = type(error).__name__ + ': ' + str(error)
        raise
    finally:
        with (output / 'installer-result.json').open('x', encoding='utf-8') as stream:
            stream.write(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
