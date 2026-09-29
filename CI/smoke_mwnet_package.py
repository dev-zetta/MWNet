#!/usr/bin/env python3
"""Check a Windows/macOS package and start its server with isolated test state."""

import argparse
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--commit', required=True)
    parser.add_argument('--source-dir', type=Path, default=Path('.'))
    args = parser.parse_args()
    header = (args.source_dir / 'components/openmw-mp/Version.hpp').read_text()
    expected_version = re.search(r'#define MWNET_VERSION "([^"]+)"', header).group(1)
    expected_protocol = re.search(r'#define MWNET_PROTO_VERSION (\d+)', header).group(1)
    root = args.root.resolve()
    macos = sys.platform == 'darwin'
    binaries = root / 'Contents/MacOS' if macos else root
    resources = root / 'Contents/Resources' if macos else root
    suffix = '' if macos else '.exe'
    for name in ('banlist.json', 'requiredDataFiles.json', 'moderation.json'):
        if not (resources / 'server/data' / name).is_file():
            raise RuntimeError(f'Package is missing initial server data: {name}')
    for name in ('cell', 'custom', 'map', 'player', 'recordstore', 'world'):
        directory = resources / 'server/data' / name
        if not directory.is_dir() or list(directory.iterdir()):
            raise RuntimeError(f'Package must contain an empty server data directory: {directory}')

    version = subprocess.run([str(binaries / ('mwnet' + suffix)), '--version'],
                             capture_output=True, text=True, timeout=30, check=True)
    output = version.stdout + version.stderr
    print(output)
    if (f'MWNet client {expected_version}' not in output
            or f'Protocol version: {expected_protocol} ' not in output
            or args.commit[:10] not in output):
        raise RuntimeError('Packaged client does not match the requested alpha source')
    if platform.machine() == 'arm64' and 'ARMv8 64-bit' not in output:
        raise RuntimeError('Packaged client reported an unexpected ARM architecture')

    with tempfile.TemporaryDirectory(prefix='mwnet-package-smoke-') as tmp:
        work = Path(tmp)
        shutil.copytree(resources / 'server', work / 'server')
        with (work / 'server/scripts/customScripts.lua').open('a') as script:
            script.write('\ncustomEventHooks.registerHandler("OnServerPostInit", '
                         'function() mwnet.StopServer(0) end)\n')
        env = os.environ.copy()
        env.update(MWNET_CONTENT_DATA_DIR='', MWNET_CONTENT_FILES='')
        result = subprocess.run([str(binaries / ('mwnet-server' + suffix))],
                                cwd=work, env=env, capture_output=True, text=True, timeout=45)
        output = result.stdout + result.stderr
        print(output)
        if result.returncode != 0:
            if macos and result.returncode < 0:
                diagnostic = subprocess.run([
                    'lldb', '--batch', '-o', 'settings set target.disable-aslr false',
                    '-o', 'run', '--one-line-on-crash', 'thread backtrace all', '--',
                    str(binaries / 'mwnet-server')], cwd=work, env=env,
                    capture_output=True, text=True, timeout=60)
                print(diagnostic.stdout + diagnostic.stderr)
            raise RuntimeError(f'Packaged server exited with {result.returncode}')
        for marker in ('Called "OnServerPostInit"', 'Quitting peacefully.', 'Error state: false'):
            if marker not in output:
                raise RuntimeError(f'Packaged server did not report: {marker}')
        if any(marker in output for marker in ('Lua error:', 'stack traceback:', 'Error state: true')):
            raise RuntimeError('Packaged server reported a script error')
    print('Package identity, CoreScripts initialization, and clean shutdown passed')


if __name__ == '__main__':
    main()
