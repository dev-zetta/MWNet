#!/usr/bin/env python3
"""Pin Docker classic/containerd image IDs to digests from the accepted build."""
import argparse
import json
from pathlib import Path
import re
import subprocess

DIGEST = re.compile(r'sha256:[0-9a-f]{64}')
FIELDS = ('config', 'manifest', 'index')
FORMAT = ('{"id":{{json .Id}},"os":{{json .Os}},"architecture":{{json .Architecture}},'
          '"revision":{{json (index .Config.Labels "org.opencontainers.image.revision")}}}')


def validate(actual, expected):
    digests = expected['imageDigests']
    if set(digests) != set(FIELDS) or any(not isinstance(d, str) or not DIGEST.fullmatch(d) for d in digests.values()):
        raise ValueError('Invalid accepted image digests')
    if not isinstance(expected['runtimeSourceCommit'], str) or not re.fullmatch('[0-9a-f]{40}', expected['runtimeSourceCommit']):
        raise ValueError('Invalid accepted source revision')
    kind = next((key for key in FIELDS if digests[key] == actual.get('id')), None)
    if kind is None:
        raise ValueError('Unaccepted game image ID: ' + str(actual.get('id')))
    if (actual.get('os') != 'linux' or actual.get('architecture') != 'amd64'
            or actual.get('revision') != expected['runtimeSourceCommit']):
        raise ValueError('Game image platform or source revision differs from the accepted build')
    return kind


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', required=True, type=Path)
    parser.add_argument('--image', required=True)
    args = parser.parse_args()
    expected = json.loads(args.manifest.read_text())
    result = subprocess.run(['docker', 'image', 'inspect', '--format', FORMAT, args.image],
                            check=True, capture_output=True, text=True)
    actual = json.loads(result.stdout)
    kind = validate(actual, expected)
    print(f'Accepted game image: {kind}={actual["id"]}; linux/amd64; revision={actual["revision"]}', flush=True)


if __name__ == '__main__':
    try:
        main()
    except (KeyError, TypeError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit('Game image verification failed: ' + str(error))
