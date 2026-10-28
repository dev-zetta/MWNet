#!/usr/bin/env python3
"""Offline bootstrap safeguards; no package installation or host configuration."""
from pathlib import Path
import subprocess
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "docker/discovery/setup-host.sh"


class HostSetupTests(unittest.TestCase):
    def shell(self, body, *args):
        # Source functions without invoking main. Mask every privileged/network
        # operation used by the installer; individual tests supply safe mocks.
        guards = """
apt-get() { echo 'unexpected apt' >&2; return 97; }
curl() { echo 'unexpected curl' >&2; return 97; }
install() { echo 'unexpected install' >&2; return 97; }
verify_runtime() { echo 'unexpected service verification' >&2; return 97; }
"""
        return subprocess.run(
            ["bash", "-c", 'source "$1"; shift\n' + guards + body, "test", str(SCRIPT), *args],
            text=True, capture_output=True, timeout=10,
        )

    def test_plan_and_help_do_not_call_preflight_or_install(self):
        for option in ("--help", "--dry-run"):
            result = self.shell("""
preflight() { echo unexpected >&2; exit 98; }
main "$1"
""", option)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("Ubuntu Server 24.04", result.stdout)
            self.assertIn("26.04 LTS", result.stdout)

    def test_invalid_arguments_stop(self):
        for args in (("--unknown",), ("--dry-run", "extra"), ("",)):
            result = self.shell('main "$@"', *args)
            self.assertNotEqual(result.returncode, 0)

    def test_only_target_platform_is_accepted(self):
        for distro, version, arch, accepted in (
            ("ubuntu", "24.04", "amd64", True),
            ("ubuntu", "26.04", "amd64", True),
            ("debian", "24.04", "amd64", False),
            ("debian", "26.04", "amd64", False),
            ("ubuntu", "24.04", "arm64", False),
            ("ubuntu", "26.04", "arm64", False),
            ("ubuntu", "22.04", "amd64", False),
            ("ubuntu", "26.10", "amd64", False),
            ("ubuntu", "26.04\nSuites: malicious", "amd64", False),
        ):
            with self.subTest(distro=distro, version=version, arch=arch):
                result = self.shell('validate_platform "$@"', distro, version, arch)
                self.assertEqual(result.returncode == 0, accepted, result.stderr)

    def test_sources_and_key_urls_follow_validated_version(self):
        for version, suite in (("24.04", "noble"), ("26.04", "resolute")):
            with self.subTest(version=version):
                result = self.shell("""
export UBUNTU_CODENAME=malicious VERSION_CODENAME=malicious
validate_platform ubuntu "$1" amd64
docker_source "$1"
tailscale_source "$1"
tailscale_key_url "$1"
""", version)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(f"Suites: {suite}\n", result.stdout)
                self.assertIn(f"https://pkgs.tailscale.com/stable/ubuntu {suite} main\n", result.stdout)
                self.assertIn(f"https://pkgs.tailscale.com/stable/ubuntu/{suite}.noarmor.gpg\n", result.stdout)
                self.assertNotIn("malicious", result.stdout)

    def test_unsupported_version_stops_before_package_operations(self):
        for version in ("", "22.04", "resolute", "26.04\nSuites: malicious"):
            with self.subTest(version=version):
                result = self.shell('install_requirements "$1"', version)
                self.assertNotEqual(result.returncode, 0)
                self.assertNotIn("unexpected", result.stderr)

    def test_main_propagates_preflight_version_and_stops_on_failure(self):
        result = self.shell("""
preflight() { printf '26.04\\n'; }
install_requirements() { printf 'install-version=%s\\n' "$1"; }
main
""")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("install-version=26.04\n", result.stdout)
        result = self.shell("""
preflight() { return 23; }
install_requirements() { echo incorrectly-installed; }
main
""")
        self.assertEqual(result.returncode, 23, result.stderr)
        self.assertNotIn("incorrectly-installed", result.stdout)

    def test_conflicts_fail_but_removed_packages_do_not(self):
        result = self.shell("""
dpkg-query() { printf 'install ok installed'; }
check_conflicts
""")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("migrate manually", result.stderr)
        result = self.shell("""
dpkg-query() { printf 'deinstall ok config-files'; }
check_conflicts
""")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_custom_sources_and_symlinks_are_preserved(self):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / "source"
            for content, accepted in ((None, True), ("expected\n", True), ("custom\n", False)):
                if content is not None:
                    source.write_text(content)
                result = self.shell('check_source "$1" expected', str(source))
                self.assertEqual(result.returncode == 0, accepted, result.stderr)
                if content is not None:
                    self.assertEqual(source.read_text(), content)
            link = Path(folder) / "link"
            link.symlink_to(source)
            self.assertNotEqual(self.shell('check_source "$1" custom', str(link)).returncode, 0)
            self.assertEqual(source.read_text(), "custom\n")

    def test_failed_download_stops_before_repository_changes(self):
        result = self.shell("""
apt-get() { return 0; }
curl() { return 22; }
install_requirements 26.04
echo incorrectly-completed
""")
        self.assertEqual(result.returncode, 22, result.stderr)
        self.assertNotIn("unexpected install", result.stderr)
        self.assertNotIn("incorrectly-completed", result.stdout)

    def test_successful_install_with_mocked_host_operations(self):
        for version, suite in (("24.04", "noble"), ("26.04", "resolute")):
            with self.subTest(version=version):
                result = self.shell("""
apt-get() { printf 'apt %s\n' "$*"; }
curl() { printf 'curl %s\n' "$*"; printf 'test key' > "${@: -1}"; }
install() {
    printf 'install %s\n' "$*"
    case "${@: -1}" in
        /etc/apt/sources.list.d/docker.sources|/etc/apt/sources.list.d/tailscale.list)
            cat "${@: -2:1}" ;;
    esac
}
verify_runtime() { echo verified; }
install_requirements "$1"
""", version)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("docker-compose-plugin tailscale", result.stdout)
                self.assertIn("/etc/apt/sources.list.d/docker.sources", result.stdout)
                self.assertIn(f"Suites: {suite}\n", result.stdout)
                self.assertIn(f"https://pkgs.tailscale.com/stable/ubuntu {suite} main\n", result.stdout)
                self.assertIn(f"https://pkgs.tailscale.com/stable/ubuntu/{suite}.noarmor.gpg", result.stdout)
                self.assertTrue(result.stdout.rstrip().endswith("verified"))


if __name__ == "__main__":
    unittest.main()
