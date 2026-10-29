#!/usr/bin/env python3
"""Image pinning across classic and containerd stores, using accepted build digests."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('gameplay_image', ROOT / 'docker/discovery/gameplay/verify_image.py')
IMAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(IMAGE)


class ImageTests(unittest.TestCase):
    def setUp(self):
        self.expected = {'imageDigests': dict(zip(IMAGE.FIELDS, ('sha256:' + c * 64 for c in 'abc'))),
                         'runtimeSourceCommit': 'd' * 40}
        self.actual = {'id': self.expected['imageDigests']['config'], 'os': 'linux',
                       'architecture': 'amd64', 'revision': 'd' * 40}

    def test_classic_config_and_containerd_manifest_or_index(self):
        for kind, digest in self.expected['imageDigests'].items():
            with self.subTest(kind=kind):
                self.actual['id'] = digest
                self.assertEqual(IMAGE.validate(self.actual, self.expected), kind)

    def test_unaccepted_image_is_rejected(self):
        self.actual['id'] = 'sha256:' + 'e' * 64
        with self.assertRaisesRegex(ValueError, 'Unaccepted'):
            IMAGE.validate(self.actual, self.expected)

    def test_wrong_platform_or_revision_is_rejected(self):
        for key, value in (('os', 'windows'), ('architecture', 'arm64'), ('revision', 'e' * 40)):
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, 'platform or source'):
                IMAGE.validate(dict(self.actual, **{key: value}), self.expected)

    def test_invalid_digest_is_rejected(self):
        self.expected['imageDigests']['index'] = 'latest'
        with self.assertRaisesRegex(ValueError, 'Invalid accepted'):
            IMAGE.validate(self.actual, self.expected)


if __name__ == '__main__':
    unittest.main()
