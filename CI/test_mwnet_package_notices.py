#!/usr/bin/env python3
"""Exercise actual notice generation and Linux archive delivery, without builds."""

import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("mwnet_linux_packager", ROOT / "CI/package_mwnet_linux.py")
PACKAGER = importlib.util.module_from_spec(spec)
spec.loader.exec_module(PACKAGER)
spec = importlib.util.spec_from_file_location("mwnet_notice_inventory", ROOT / "CI/inventory_mwnet_package_licenses.py")
INVENTORY = importlib.util.module_from_spec(spec)
spec.loader.exec_module(INVENTORY)


class NoticeDelivery(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.revision = subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip()

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="mwnet-notice-test-")
        self.addCleanup(self.tmp.cleanup)
        self.work = Path(self.tmp.name)

    def package(self, server_only=False):
        build = self.work / "build"
        build.mkdir()
        for name in ("mwnet-server", "mwnet"):
            (build / name).write_text("test executable placeholder\n")
            (build / name).chmod(0o755)
        for name in ("mwnet-server-default.cfg", "mwnet-client-default.cfg", "defaults.bin", "openmw.cfg"):
            (build / name).write_text("fixture\n")
        (build / "resources").mkdir()
        sbom = self.work / "sbom.json"
        sbom.write_text(json.dumps({"packages": [{"name": "MWNet", "sourceInfo": "Git revision: " + self.revision}]}))
        arguments = ["packager", "--source-dir", str(ROOT), "--build-dir", str(build),
                     "--output-dir", str(self.work / "packages"), "--sbom", str(sbom), "--commit", self.revision]
        if server_only:
            arguments.append("--server-only")
        run = subprocess.run

        def command(args, **kwargs):
            if args[0] == "ldd":
                return subprocess.CompletedProcess(args, 0, "libc.so.6 => /system/libc.so.6\n")
            return run(args, **kwargs)

        before = hashlib.sha256((ROOT / "LICENSE").read_bytes()).hexdigest()
        with mock.patch.object(sys, "argv", arguments), mock.patch.object(PACKAGER.subprocess, "run", side_effect=command), contextlib.redirect_stdout(io.StringIO()):
            PACKAGER.main()
        self.assertEqual(before, hashlib.sha256((ROOT / "LICENSE").read_bytes()).hexdigest())
        return next((self.work / "packages").glob("*.tar.gz"))

    def verify_archive(self, archive):
        with tarfile.open(archive) as tar:
            root = tar.getnames()[0]

            def read(name):
                return tar.extractfile(root + "/" + name).read()

            notices = read("licenses/THIRD_PARTY_NOTICES.txt")
            provenance = json.loads(read("licenses/notice-inputs.json"))
            self.assertEqual(provenance["runtimeSourceCommit"], self.revision)
            self.assertEqual(provenance["noticeSha256"], hashlib.sha256(notices).hexdigest())
            self.assertEqual(len(provenance["inputs"]), 15)
            dkjson_path = "files/mwnet/core-scripts/lib/lua/dkjson.lua"
            self.assertEqual(read(dkjson_path), (ROOT / dkjson_path).read_bytes())
            for key, marker in INVENTORY.MARKERS.items():
                if key not in {"LGPL-title", "OpenSSL"}:
                    self.assertIn(marker.casefold().encode(), notices.lower())
            for word in (b"yaml-cpp", b"RecastNavigation", b"GameNetworkingSockets"):
                self.assertIn(word, read("licenses/FETCHED_SOURCES.txt"))
            self.assertIn(self.revision.encode(), read("CORRESPONDING_SOURCE.txt"))
            self.assertIn(b"CI/build_mwnet_linux.sh and Dockerfile.mwnet", read("CORRESPONDING_SOURCE.txt"))
            self.assertIn(b"licenses/FETCHED_SOURCES.txt", read("CORRESPONDING_SOURCE.txt"))
            manifest = json.loads(read("manifest.json"))
            for name in ("licenses/THIRD_PARTY_NOTICES.txt", "licenses/notice-inputs.json", "licenses/FETCHED_SOURCES.txt", "CORRESPONDING_SOURCE.txt"):
                self.assertEqual(manifest["files"][name], hashlib.sha256(read(name)).hexdigest())
        evidence = INVENTORY.inventory(archive)
        for key, matches in evidence["noticeMarkerMatches"].items():
            if key not in {"LGPL-title", "OpenSSL"}:
                self.assertTrue(matches, key)

    def test_client_server_delivers_hashed_notices_and_source_directions(self):
        self.verify_archive(self.package())

    def test_server_only_retains_compiled_notices(self):
        self.verify_archive(self.package(server_only=True))

    def reject_manifest(self, alter, message):
        shutil.copytree(ROOT / "docs/license/third-party", self.work / "inputs")
        manifest = self.work / "inputs/manifest.json"
        entries = json.loads(manifest.read_text())
        alter(entries)
        manifest.write_text(json.dumps(entries))
        result = subprocess.run([sys.executable, str(ROOT / "CI/collect_mwnet_third_party_notices.py"),
                                 "--source-dir", str(ROOT), "--source-ref", self.revision,
                                 "--static-manifest", str(manifest), "--output-dir", str(self.work / "output")],
                                text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(message, result.stderr)
        self.assertFalse((self.work / "output").exists())

    def test_changed_notice_bytes_fail_before_output(self):
        self.reject_manifest(lambda j: j["entries"][0].update(sha256="0" * 64), "notice bytes differ")

    def test_stale_dependency_pin_fails_before_output(self):
        self.reject_manifest(lambda j: j["entries"][0].update(sourcePin="invalid-source-pin"), "pin differs from runtime source")


if __name__ == "__main__":
    unittest.main()
