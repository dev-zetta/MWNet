#!/usr/bin/env python3
"""Container scratch must use the artifact disk, even with a hostile TMPDIR."""
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'CI'))
from mwnet_temp import prepare_temp_root


class ContainerTemporaryStorageTests(unittest.TestCase):
    def prepare(self, directory):
        root = Path(directory)
        tools = root / 'tools'
        for name in ('bin/mwnet-headless-integration', 'bin/mwnet-directory',
                     'bin/mwnet-discovery-tests', 'caddy', 'mwnet', 'mwnet-server',
                     'mwnet-directory'):
            path = tools / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#!/bin/sh\nexit 0\n')
            path.chmod(0o700)
        (tools / 'evidence').mkdir()
        (tools / 'evidence/compiled-provenance.json').write_text('{}')
        data = root / 'data files'
        data.mkdir()
        package = root / 'package'
        package.mkdir()
        (package / 'manifest.json').write_text('{}')
        proof = root / 'native-proof.json'
        proof.write_text('{}')
        fixture = root / 'completed' / 'case'
        (fixture / 'server/data').mkdir(parents=True)
        (fixture.parent / 'gameplay-receipt.json').write_text('{}')
        capture = root / 'docker-arguments.jsonl'
        fake = root / 'docker'
        fake.write_text(
            '#!/usr/bin/env python3\n'
            'import json,os,sys\n'
            'with open(os.environ["CAPTURE"],"a") as f:\n'
            ' f.write(json.dumps({"args":sys.argv[1:],'
            '"tmp":{k:os.environ.get(k) for k in ("TMPDIR","TMP","TEMP")}})+"\\n")\n')
        fake.chmod(0o700)
        environment = dict(os.environ, PATH=str(root) + ':' + os.environ['PATH'],
                           CAPTURE=str(capture), TMPDIR='/tmp/do-not-use-this',
                           TMP='/tmp/do-not-use-this', TEMP='/tmp/do-not-use-this',
                           MWNET_DOCKER_CONTEXT='default')
        return tools, data, package, proof, fixture, capture, environment

    def commands(self, root, inputs):
        tools, data, package, proof, fixture, *_ = inputs
        return {
            'overnight': [str(tools), str(root / 'overnight output'), '--preflight', '--seconds', '30'],
            'gameplay': [str(tools), str(data), str(root / 'gameplay output')],
            'recovery': [str(package), str(tools), str(proof), str(fixture), str(root / 'recovery output')],
        }

    def test_all_wrappers_bind_private_disk_tmp_and_override_inherited_environment(self):
        with tempfile.TemporaryDirectory(prefix='container tmp ', dir=prepare_temp_root()) as directory:
            root = Path(directory)
            inputs = self.prepare(directory)
            capture, environment = inputs[-2:]
            for name, arguments in self.commands(root, inputs).items():
                with self.subTest(wrapper=name):
                    capture.unlink(missing_ok=True)
                    result = subprocess.run(
                        ['bash', str(ROOT / f'CI/run_mwnet_{name}_container.sh'), *arguments],
                        env=environment, capture_output=True, text=True, timeout=10)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    invocations = [json.loads(line) for line in capture.read_text().splitlines()]
                    invocation = next(entry for entry in invocations if 'run' in entry['args'])
                    args = invocation['args']
                    mounts = [args[index + 1] for index, argument in enumerate(args) if argument == '--mount']
                    scratch_mount = next(mount for mount in mounts if mount.endswith(',dst=/tmp'))
                    scratch = Path(scratch_mount.removeprefix('type=bind,src=').removesuffix(',dst=/tmp'))
                    self.assertEqual(scratch.parent, root)
                    self.assertTrue(scratch.name.startswith(f'.mwnet-tmp.{name} output.'))
                    self.assertEqual(stat.S_IMODE(scratch.stat().st_mode), 0o700)
                    self.assertEqual(scratch.stat().st_uid, os.getuid())
                    self.assertEqual(set(invocation['tmp'].values()), {str(scratch)})
                    self.assertNotIn('--tmpfs', args)
                    for variable in ('TMPDIR', 'TMP', 'TEMP'):
                        self.assertIn(f'{variable}=/tmp', args)

    def test_all_wrappers_reject_ram_backed_artifacts_before_starting_docker(self):
        with tempfile.TemporaryDirectory(prefix='container tmp ', dir=prepare_temp_root()) as directory:
            root = Path(directory)
            inputs = self.prepare(directory)
            capture, environment = inputs[-2:]
            fake_df = root / 'df'
            fake_df.write_text('#!/bin/sh\nprintf "Type\\ntmpfs\\n"\n')
            fake_df.chmod(0o700)
            for name, arguments in self.commands(root, inputs).items():
                with self.subTest(wrapper=name):
                    result = subprocess.run(
                        ['bash', str(ROOT / f'CI/run_mwnet_{name}_container.sh'), *arguments],
                        env=environment, capture_output=True, text=True, timeout=10)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn('must be on persistent disk', result.stderr)
                    self.assertFalse(capture.exists())
                    self.assertFalse(list(root.glob('.mwnet-tmp.*')))


if __name__ == '__main__':
    unittest.main()
