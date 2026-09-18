#!/usr/bin/env python3
"""Verify a candidate archive and test client version and isolated server startup/shutdown."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import tarfile
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    expected = args.archive.with_suffix(args.archive.suffix + '.sha256').read_text().split()[0]
    with args.archive.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    if digest != expected:
        raise RuntimeError('archive checksum mismatch')
    with tempfile.TemporaryDirectory(prefix='tes3mp-smoke-', dir=args.output_dir) as tmp:
        work = Path(tmp).resolve()
        with tarfile.open(args.archive) as archive:
            archive.extractall(work, filter='data')
        roots = list(work.iterdir())
        if len(roots) != 1 or not roots[0].is_dir():
            raise RuntimeError('expected one package root')
        root = roots[0]
        manifest = json.loads((root / 'manifest.json').read_text())
        actual = {str(p.relative_to(root)) for p in root.rglob('*') if p.is_file()}
        if actual != set(manifest['files']) | {'manifest.json'}:
            raise RuntimeError('manifest file inventory mismatch')
        for name, expected_hash in manifest['files'].items():
            with (root / name).open('rb') as stream:
                if hashlib.file_digest(stream, 'sha256').hexdigest() != expected_hash:
                    raise RuntimeError(f'file checksum mismatch: {name}')
        environment = os.environ.copy()
        environment.pop('TES3MP_SERVER_PROFILE', None)
        environment['TES3MP_BUILD_DIR'] = str(root / 'build')
        environment['TES3MP_TEST_ROOT'] = str(work / 'state')
        if (root / 'build/tes3mp').is_file():
            runtime = work / 'runtime'
            runtime.mkdir(mode=0o700)
            environment['XDG_RUNTIME_DIR'] = str(runtime)
            environment['XDG_CONFIG_HOME'] = str(work / 'client-config')
            environment['XDG_DATA_HOME'] = str(work / 'client-data')
            environment['XDG_CACHE_HOME'] = str(work / 'client-cache')
            environment['OPENMW_DISABLE_CRASH_CATCHER'] = '1'
            with (args.output_dir / 'client-version.log').open('w') as output:
                subprocess.run([str(root / 'build/tes3mp'), '--version'],
                               cwd=root / 'build', env=environment, stdout=output,
                               stderr=subprocess.STDOUT, timeout=30, check=True)
        log = args.output_dir / 'server-startup.log'
        with log.open('w') as output:
            process = subprocess.Popen(['bash', str(root / 'run-tes3mp-server.sh')],
                                       cwd=root, env=environment, stdout=output,
                                       stderr=subprocess.STDOUT, start_new_session=True)
            try:
                deadline = time.monotonic() + 30
                while time.monotonic() < deadline:
                    if process.poll() is not None:
                        raise RuntimeError(f'server exited before shutdown: {process.returncode}; see {log}')
                    if 'Called "OnServerPostInit"' in log.read_text(errors='replace'):
                        break
                    time.sleep(0.1)
                else:
                    raise RuntimeError(f'CoreScripts startup timed out; see {log}')
                os.killpg(process.pid, signal.SIGINT)
                if process.wait(timeout=10) != 0:
                    raise RuntimeError(f'server shutdown failed; see {log}')
            finally:
                if process.poll() is None:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
        output = log.read_text(errors='replace')
        if 'Quitting peacefully.' not in output or 'Error state: false' not in output:
            raise RuntimeError(f'expected error-free graceful shutdown markers; see {log}')
        if any(marker in output for marker in ('Lua error:', 'stack traceback:', 'Error state: true')):
            raise RuntimeError(f'script error during startup or shutdown; see {log}')
    print('Archive hashes, included client version check, CoreScripts initialization and graceful shutdown passed')


if __name__ == '__main__':
    main()
