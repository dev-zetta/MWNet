#!/usr/bin/env python3
"""Offline appliance safeguards; never installs packages or starts containers."""
import hashlib
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

import yaml


ROOT = Path(__file__).resolve().parents[1]
APPLIANCE = ROOT / "docker/discovery/appliance"
SPEC = importlib.util.spec_from_file_location("discovery_iso", APPLIANCE / "build-iso.py")
BUILDER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILDER)


class BaseVerificationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        folder = Path(self.temporary.name)
        self.iso = folder / "ubuntu-24.04.3-live-server-amd64.iso"
        self.iso.write_bytes(b"small offline ISO fixture")
        self.checksum = hashlib.sha256(self.iso.read_bytes()).hexdigest()
        self.sums = folder / "SHA256SUMS"
        self.sums.write_text(f"{self.checksum} *{self.iso.name}\n")
        self.signature = folder / "SHA256SUMS.gpg"
        self.keyring = folder / "test-keyring.gpg"

    def verify(self):
        return BUILDER.verify_base(self.iso, self.sums, self.signature, self.keyring)

    def test_verification_is_required_before_hashing(self):
        rejected = subprocess.CalledProcessError(2, ["gpgv"])
        with mock.patch.object(BUILDER, "run", side_effect=rejected) as run, \
                mock.patch.object(BUILDER, "digest") as digest:
            with self.assertRaises(subprocess.CalledProcessError):
                self.verify()
            run.assert_called_once_with("gpgv", "--keyring", self.keyring, self.signature, self.sums)
            digest.assert_not_called()

    @unittest.skipUnless(shutil.which("gpgv"), "gpgv is not installed")
    def test_missing_signature_fails_with_real_verifier(self):
        self.keyring.write_bytes(b"")
        with self.assertRaises(subprocess.CalledProcessError):
            self.verify()

    def test_matching_digest_is_accepted_after_signature_verification(self):
        with mock.patch.object(BUILDER, "run") as verifier:
            self.assertEqual(self.verify(), self.checksum)
        verifier.assert_called_once()

    def test_supported_lts_releases_and_rejection_of_other_media(self):
        for version in ('24.04', '24.04.5', '26.04', '26.04.1'):
            with self.subTest(version=version), mock.patch.object(BUILDER, 'run'):
                self.iso = self.iso.with_name(f'ubuntu-{version}-live-server-amd64.iso')
                self.iso.write_bytes(b'small offline ISO fixture')
                self.sums.write_text(f'{self.checksum} *{self.iso.name}\n')
                self.assertEqual(self.verify(), self.checksum)
                self.assertEqual(BUILDER.host_release(self.iso), version[:5])
        for name in ('ubuntu-26.10-live-server-amd64.iso', 'ubuntu-26.04.1-desktop-amd64.iso',
                     'ubuntu-26.04-live-server-arm64.iso', 'ubuntu-22.04-live-server-amd64.iso'):
            with self.subTest(name=name), mock.patch.object(BUILDER, 'run') as verifier:
                self.iso = self.iso.with_name(name)
                with self.assertRaisesRegex(ValueError, 'ISO filename'):
                    self.verify()
                verifier.assert_not_called()

    def test_altered_iso_and_ambiguous_checksums_are_rejected(self):
        for sums in (
            f"{'0' * 64} *{self.iso.name}\n",
            f"{self.checksum} *another.iso\n",
            f"{self.checksum} *{self.iso.name}\n" * 2,
        ):
            with self.subTest(sums=sums), mock.patch.object(BUILDER, "run"):
                self.sums.write_text(sums)
                with self.assertRaisesRegex(ValueError, "signed checksum"):
                    self.verify()

    def test_signature_argument_cannot_be_omitted(self):
        result = subprocess.run(
            ["python3", str(APPLIANCE / "build-iso.py"), "--base-iso", str(self.iso),
             "--checksums", str(self.sums), "--output", str(self.iso.parent / "output.iso")],
            text=True, capture_output=True, timeout=10,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("--signature", result.stderr)
        self.assertFalse((self.iso.parent / "output.iso").exists())


class ConfigurationTests(unittest.TestCase):
    def test_disk_and_account_setup_remain_interactive(self):
        config = yaml.safe_load((APPLIANCE / "autoinstall.yaml").read_text())["autoinstall"]
        self.assertEqual(config["interactive-sections"], ["*"])
        for key in ("storage", "identity", "user-data"):
            self.assertNotIn(key, config)
        self.assertNotIn("authorized-keys", config.get("ssh", {}))
        self.assertFalse(config["ssh"]["allow-pw"])
        self.assertNotIn("autoinstall", " ".join(config["late-commands"]))

    def test_runtime_compose_uses_only_packaged_images_and_private_backend(self):
        config = BUILDER.runtime_compose("mwnet-directory:offline-test")
        services = config["services"]
        self.assertEqual(services["directory"]["image"], "mwnet-directory:offline-test")
        self.assertNotIn("ports", services["directory"])
        for service in services.values():
            self.assertNotIn("build", service)
            self.assertEqual(service["pull_policy"], "never")
            self.assertNotEqual(service.get("network_mode"), "host")
        self.assertEqual(set(services["https"]["ports"]), {"80:80", "443:443"})


@unittest.skipUnless(shutil.which("git"), "git is not installed")
class SourceArchiveTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name)
        self.repo = self.folder / "repo"
        self.repo.mkdir()
        self.environment = dict(os.environ, GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull)
        self.git("init", "--quiet")

    def git(self, *args):
        subprocess.run(["git", "-C", str(self.repo), *args], check=True,
                       capture_output=True, env=self.environment, timeout=10)

    def tracked_files(self, names):
        for name in names:
            path = self.repo / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(f"fixture for {name}\n")
        self.git("add", "--force", "--all")

    def source_context(self):
        destination = self.folder / "context"
        destination.mkdir()
        with mock.patch.object(BUILDER, "ROOT", self.repo), \
                mock.patch.dict(os.environ, self.environment):
            BUILDER.source_context(destination)
        return {str(path.relative_to(destination)) for path in destination.rglob("*") if path.is_file()}

    def test_only_tracked_build_inputs_are_archived(self):
        allowed = {"CMakeLists.txt", "cmake/helper.cmake", "components/source.cpp",
                   "apps/mwnet-directory/main.cpp", "LICENSE", "docker/discovery/Dockerfile"}
        excluded = {".env", "docker/discovery/.env", "identity.private", "server/players/alice.json"}
        self.tracked_files(allowed | excluded)
        (self.repo / "components/untracked-private.key").write_text("untracked secret")
        self.assertEqual(self.source_context(), allowed)

    def test_nested_environment_files_are_never_archived(self):
        self.tracked_files({"apps/mwnet-directory/main.cpp", "apps/.env", "components/.env.local"})
        with self.assertRaisesRegex(ValueError, "credential"):
            self.source_context()
        self.assertFalse((self.folder / "context/apps/.env").exists())
        self.assertFalse((self.folder / "context/components/.env.local").exists())

    def test_symlink_cannot_copy_external_state_into_archive(self):
        self.tracked_files({"components/source.cpp"})
        (self.folder / "private.key").write_text("outside source tree")
        (self.repo / "components/link.cpp").symlink_to(self.folder / "private.key")
        self.git("add", "--force", "--all")
        with self.assertRaisesRegex(ValueError, "regular file"):
            self.source_context()


class FirstBootTests(unittest.TestCase):
    def first_boot(self, failure=""):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            payload, state, empty_path = folder / "payload", folder / "state", folder / "empty-path"
            for path in (payload, state, empty_path):
                path.mkdir()
            (payload / "images.lock").write_text("mwnet-directory:test sha256:expected\n")
            (payload / "images.tar").write_bytes(b"fixture")
            checksum = hashlib.sha256((payload / "images.tar").read_bytes()).hexdigest()
            if failure == "checksum":
                checksum = "0" * 64
            (payload / "payload.sha256").write_text(f"{checksum}  images.tar\n")
            # An empty PATH and explicit mocks prevent commands added to first-boot
            # from accidentally invoking package managers, Docker or host writes.
            wrapper = r'''
set -euo pipefail
cd() { [[ $1 == /opt/mwnet/discovery-appliance ]] || return 98; builtin cd "$TEST_PAYLOAD"; }
sha256sum() { "$TEST_SHA256" "$@"; }
bash() { [[ $* == './setup-host.sh' ]] || return 98; echo setup; [[ $TEST_FAILURE != setup ]]; }
docker() {
    printf 'docker %s\n' "$*" >&2
    case "$1 $2" in
        'load --input') [[ $TEST_FAILURE != load ]] ;;
        'image inspect')
            if [[ $TEST_FAILURE == image ]]; then echo sha256:wrong; else echo sha256:expected; fi ;;
        'compose -p') [[ $TEST_FAILURE != compose ]] ;;
        *) return 98 ;;
    esac
}
install() {
    case "$*" in
        '-d -m 0755 /var/lib/mwnet-appliance') return 0 ;;
        '-m 0644 /dev/null /var/lib/mwnet-appliance/ready') : > "$TEST_STATE/ready" ;;
        *) return 98 ;;
    esac
}
source "$1"
'''
            environment = dict(os.environ, PATH=str(empty_path), TEST_PAYLOAD=str(payload),
                               TEST_STATE=str(state), TEST_FAILURE=failure,
                               TEST_SHA256=shutil.which("sha256sum"))
            result = subprocess.run(["/bin/bash", "-c", wrapper, "test", str(APPLIANCE / "first-boot.sh")],
                                    text=True, capture_output=True, env=environment, timeout=10)
            return result, (state / "ready").exists()

    def test_failed_stage_never_marks_host_ready(self):
        for failure in ("checksum", "setup", "load", "image", "compose"):
            with self.subTest(failure=failure):
                result, ready = self.first_boot(failure)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertFalse(ready)
                if failure == "checksum":
                    self.assertNotIn("setup", result.stdout)
                if failure == "image":
                    self.assertIn("Image mismatch", result.stderr)
                    self.assertNotIn("docker compose", result.stderr)

    def test_success_checks_images_and_compose_before_marking_host_ready(self):
        result, ready = self.first_boot()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(ready)
        self.assertIn("docker image inspect", result.stderr)
        self.assertIn("docker compose", result.stderr)
        self.assertNotIn(" up", result.stderr)
        self.assertIn("remains stopped", result.stdout)

    def test_systemd_retries_are_bounded_and_success_skips_future_setup(self):
        unit = (APPLIANCE / "mwnet-appliance-setup.service").read_text()
        self.assertIn("ConditionPathExists=!/var/lib/mwnet-appliance/ready", unit)
        self.assertIn("StartLimitBurst=3", unit)
        self.assertIn("TimeoutStartSec=30min", unit)

    def test_setup_does_not_create_a_cloud_final_boot_order_cycle(self):
        service = (APPLIANCE / "mwnet-appliance-setup.service").read_text()
        timer = (APPLIANCE / "mwnet-appliance-setup.timer").read_text()
        installer = yaml.safe_load((APPLIANCE / "autoinstall.yaml").read_text())["autoinstall"]
        self.assertIn("cloud-final.service", service)
        self.assertNotIn("WantedBy=multi-user.target", service)
        self.assertIn("OnBootSec=", timer)
        self.assertIn("WantedBy=timers.target", timer)
        self.assertIn("Unit=mwnet-appliance-setup.service", timer)
        self.assertIn("enable mwnet-appliance-setup.timer", "\n".join(installer["late-commands"]))


if __name__ == "__main__":
    unittest.main()
