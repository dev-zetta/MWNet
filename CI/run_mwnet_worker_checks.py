#!/usr/bin/env python3
"""Execute the frozen six-test inventory in a minimal acceptance worker."""
import hashlib
import json
from pathlib import Path
import subprocess


def run():
    tools = Path('/tools')
    inventory = json.loads((tools / 'evidence/ctest-inventory.json').read_text())
    hashes = json.loads((tools / 'evidence/all-eight-binary-sha256.json').read_text())
    results = []
    for test in inventory['tests']:
        binary = tools / 'bin' / Path(test['command'][0]).name
        if hashlib.sha256(binary.read_bytes()).hexdigest() != hashes[binary.name]['sha256']:
            raise ValueError('Worker check binary changed: ' + binary.name)
        limit = next((p['value'] for p in test.get('properties', []) if p['name'] == 'TIMEOUT'), 180)
        with Path('/tmp', test['name'] + '.log').open('xb') as log:
            outcome = subprocess.run([str(binary), *test['command'][1:]], stdout=log,
                                     stderr=subprocess.STDOUT, timeout=limit)
        results.append({'name': test['name'], 'exitCode': outcome.returncode,
                        'binarySha256': hashes[binary.name]['sha256']})
        if outcome.returncode:
            raise RuntimeError('Native worker check failed: ' + test['name'])
    if len(results) != 6:
        raise ValueError('Original native test inventory changed')
    Path('/tmp/checks.json').write_text(json.dumps({'passed': 6, 'tests': results}, indent=2) + '\n')


if __name__ == '__main__':
    run()
