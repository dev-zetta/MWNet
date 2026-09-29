import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest


RUNNER = Path(__file__).resolve().parents[1] / "run_mwnet_soak_pair.py"
FAKE = r'''
import json, pathlib, sys, time
config = json.loads(pathlib.Path(sys.argv[0] + ".json").read_text())
if sys.argv[1:] == ["--build-info"]:
    print(json.dumps({"addressSanitizerEnabled": config["asan"]}))
    sys.exit(0)
arguments = sys.argv[1:]
has_rss_gate = "--fail-on-memory-growth" in arguments
assert has_rss_gate is not config["asan"]
if has_rss_gate:
    arguments.remove("--fail-on-memory-growth")
options = dict(zip(arguments[::2], arguments[1::2]))
if config.get("hang"):
    time.sleep(60)
if config.get("exit"):
    sys.exit(config["exit"])
if config.get("missing_metrics"):
    sys.exit(0)
metrics = {"status": "passed", "scenariosComplete": True,
           "commit": options["--commit"], "cycles": int(options["--cycles"]),
           "clients": int(options["--clients"]),
           "simulatedLatencyMilliseconds": int(options["--latency-ms"]),
           "simulatedPacketLossPercent": int(options["--packet-loss-percent"]),
           "addressSanitizerEnabled": config["asan"],
           "monotonicMemoryGrowthDetected": config["asan"],
           "residentMemoryGrowthPercentAfterWarmup": 42 if config["asan"] else 0.2}
metrics.update(config.get("metrics", {}))
pathlib.Path(options["--metrics-output"]).write_text(json.dumps(metrics))
sys.exit(config.get("exit_after_metrics", 0))
'''


class PairedSoakTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="mwnet paired soak ")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.configure("native", asan=False)
        self.configure("sanitizer", asan=True)
        self.artifacts = self.root / "artifacts"
        self.command = [sys.executable, str(RUNNER),
                        "--native-executable", str(self.root / "native"),
                        "--sanitizer-executable", str(self.root / "sanitizer"),
                        "--artifacts-dir", str(self.artifacts),
                        "--cycles", "100", "--clients", "8", "--duration-seconds", "0",
                        "--latency-ms", "75", "--packet-loss-percent", "2",
                        "--commit", "a" * 40]

    def configure(self, name, **config):
        executable = self.root / name
        executable.write_text(f"#!{sys.executable}\n" + FAKE)
        executable.chmod(0o755)
        Path(str(executable) + ".json").write_text(json.dumps(config))

    def run_pair(self, extra=()):
        return subprocess.run(self.command + list(extra), capture_output=True, text=True, timeout=10)

    def manifest(self):
        return json.loads((self.artifacts / "manifest.json").read_text())

    def test_both_runs_required_and_sanitizer_rss_is_only_diagnostic(self):
        result = self.run_pair()
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest = self.manifest()
        self.assertEqual(manifest["status"], "passed")
        self.assertEqual(manifest["rssGateBuild"], "native")
        for run in manifest["runs"].values():
            self.assertEqual(run["exitCode"], 0)
            self.assertEqual(len(run["metricsSha256"]), 64)

    def test_sanitizer_failure_stops_other_child_and_preserves_verdicts(self):
        self.configure("native", asan=False, hang=True)
        self.configure("sanitizer", asan=True, exit=17)
        self.assertNotEqual(self.run_pair().returncode, 0)
        manifest = self.manifest()
        self.assertEqual(manifest["status"], "failed")
        self.assertEqual(manifest["runs"]["sanitizer"]["exitCode"], 17)
        self.assertNotEqual(manifest["runs"]["native"]["exitCode"], 0)
        with self.assertRaises(ProcessLookupError):
            os.kill(manifest["runs"]["native"]["pid"], 0)

    def test_native_growth_rejects_even_a_zero_exit(self):
        self.configure("native", asan=False, metrics={"residentMemoryGrowthPercentAfterWarmup": 2})
        self.assertNotEqual(self.run_pair().returncode, 0)
        self.assertIn("native RSS growth", self.manifest()["failure"])

    def test_partial_report_rejects_even_a_zero_exit(self):
        self.configure("native", asan=False,
                       metrics={"status": "incomplete", "scenariosComplete": False})
        self.assertNotEqual(self.run_pair().returncode, 0)
        self.assertIn("incomplete or failed", self.manifest()["failure"])

    def test_partial_report_is_hashed_on_child_failure(self):
        self.configure("native", asan=False, exit_after_metrics=1,
                       metrics={"status": "incomplete", "scenariosComplete": False})
        self.assertNotEqual(self.run_pair().returncode, 0)
        report = self.artifacts / "native" / "metrics.json"
        self.assertEqual(self.manifest()["runs"]["native"]["metricsSha256"],
                         hashlib.sha256(report.read_bytes()).hexdigest())

    def test_wrong_candidate_rejects_successful_processes(self):
        self.configure("sanitizer", asan=True, metrics={"commit": "b" * 40})
        self.assertNotEqual(self.run_pair().returncode, 0)
        self.assertIn("candidate/workload", self.manifest()["failure"])

    def test_missing_metrics_rejects_successful_processes(self):
        self.configure("sanitizer", asan=True, missing_metrics=True)
        self.assertNotEqual(self.run_pair().returncode, 0)
        self.assertEqual(self.manifest()["status"], "failed")

    def test_mislabeled_build_is_rejected_before_workload_starts(self):
        self.configure("native", asan=True)
        result = self.run_pair()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("wrong sanitizer configuration", result.stderr)
        self.assertFalse((self.artifacts / "native").exists())

    def test_short_release_run_is_rejected(self):
        result = self.run_pair(["--release-gates"])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("full eight-client 24-hour workload", result.stderr)

    def test_existing_artifacts_are_preserved(self):
        (self.artifacts / "native").mkdir(parents=True)
        sentinel = self.artifacts / "native" / "soak.log"
        sentinel.write_text("previous evidence")
        self.assertNotEqual(self.run_pair().returncode, 0)
        self.assertEqual(sentinel.read_text(), "previous evidence")

    def test_termination_stops_both_children_and_records_failure(self):
        for name in ("native", "sanitizer"):
            self.configure(name, asan=name == "sanitizer", hang=True)
        with subprocess.Popen(self.command, stdout=subprocess.PIPE, stderr=subprocess.PIPE) as process:
            try:
                deadline = time.monotonic() + 5
                while not (self.artifacts / "manifest.json").exists():
                    self.assertLess(time.monotonic(), deadline)
                    time.sleep(0.02)
            finally:
                process.terminate()
                process.communicate(timeout=5)
            self.assertNotEqual(process.returncode, 0)
        manifest = self.manifest()
        self.assertEqual(manifest["status"], "failed")
        for run in manifest["runs"].values():
            with self.assertRaises(ProcessLookupError):
                os.kill(run["pid"], 0)


if __name__ == "__main__":
    unittest.main()
