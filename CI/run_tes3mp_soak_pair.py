#!/usr/bin/env python3
"""Require native RSS stability and a separate, fully instrumented soak."""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import signal
import subprocess
import sys
import time
from datetime import datetime, timezone


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_manifest(root, manifest):
    temporary = root / "manifest.json.tmp"
    temporary.write_text(json.dumps(manifest, indent=2) + "\n")
    temporary.replace(root / "manifest.json")


def memory_constraints():
    result = {}
    for name in ("memory.max", "memory.swap.max", "memory.events"):
        try:
            value = (Path("/sys/fs/cgroup") / name).read_text().strip()
        except OSError:
            continue
        result[name] = (dict((key, int(count)) for key, count in
                            (line.split() for line in value.splitlines()))
                        if name == "memory.events" else value)
    return result


def observe_process(process, result):
    # VmHWM includes short KDF peaks between RSS samples. Other platforms still
    # retain the executable's own metrics, without claiming a kernel peak.
    try:
        fields = dict(line.split(":", 1) for line in
                      Path(f"/proc/{process.pid}/status").read_text().splitlines()
                      if ":" in line)
    except OSError:
        return
    if "VmHWM" in fields:
        result["peakResidentBytes"] = max(result.get("peakResidentBytes", 0),
                                          int(fields["VmHWM"].split()[0]) * 1024)


def run_pair(args):
    root = args.artifacts_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    if any((root / name).exists() for name in ("manifest.json", "native", "sanitizer")):
        raise ValueError("paired soak artifacts already exist; choose a fresh directory")
    constraints = memory_constraints()
    limit = constraints.get("memory.max", "max")
    if args.release_gates and limit != "max" and int(limit) < 2 * 1024 ** 3:
        raise ValueError("paired release soak needs a container memory limit of at least 2 GiB")

    sanitizer_env = os.environ.copy()
    sanitizer_env["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1"
    sanitizer_env["UBSAN_OPTIONS"] = "print_stacktrace=1:halt_on_error=1"
    executables = {"native": args.native_executable.resolve(),
                   "sanitizer": args.sanitizer_executable.resolve()}
    if executables["native"].samefile(executables["sanitizer"]):
        raise ValueError("native and sanitizer executables must be different builds")
    for name, executable in executables.items():
        info = subprocess.run([str(executable), "--build-info"], check=True,
                              capture_output=True, text=True, timeout=15,
                              env=sanitizer_env if name == "sanitizer" else None)
        if json.loads(info.stdout).get("addressSanitizerEnabled") is not (name == "sanitizer"):
            raise ValueError(f"{name} executable has the wrong sanitizer configuration")

    manifest = {"schemaVersion": 1, "commit": args.commit, "status": "running",
                "startedAt": utc_now(), "rssGrowthLimitPercent": 1,
                "rssGateBuild": "native", "releaseGates": args.release_gates,
                "memoryConstraintsAtStart": constraints,
                "sanitizerOptions": {key: sanitizer_env[key]
                                     for key in ("ASAN_OPTIONS", "UBSAN_OPTIONS")},
                "runs": {}}
    processes, streams, started = {}, {}, {}
    failure = None
    try:
        for name, executable in executables.items():
            directory = root / name
            directory.mkdir()
            command = [str(executable), "--cycles", str(args.cycles),
                       "--clients", str(args.clients),
                       "--duration-seconds", str(args.duration_seconds),
                       "--latency-ms", str(args.latency_ms),
                       "--packet-loss-percent", str(args.packet_loss_percent),
                       "--state-dir", str(directory / "state"),
                       "--metrics-output", str(directory / "metrics.json"),
                       "--commit", args.commit]
            if name == "native":
                command.append("--fail-on-memory-growth")
            streams[name] = (directory / "soak.log").open("w")
            manifest["runs"][name] = {"executableSha256": sha256(executable),
                                       "command": command}
            started[name] = time.monotonic()
            processes[name] = subprocess.Popen(command, stdout=streams[name],
                                                stderr=subprocess.STDOUT,
                                                env=sanitizer_env if name == "sanitizer" else None)
            manifest["runs"][name]["pid"] = processes[name].pid
        write_manifest(root, manifest)
        next_report = time.monotonic() + 60
        while True:
            for name, process in processes.items():
                result = manifest["runs"][name]
                code = process.poll()
                if code is None:
                    observe_process(process, result)
                if code is not None and "exitCode" not in result:
                    result.update(exitCode=code, elapsedSeconds=time.monotonic() - started[name])
                    (root / name / "exit-code.txt").write_text(f"{code}\n")
                if code not in (None, 0):
                    raise RuntimeError(f"{name} soak exited {code}; inspect {root / name / 'soak.log'}")
            if all(process.poll() is not None for process in processes.values()):
                break
            if time.monotonic() >= next_report:
                write_manifest(root, manifest)
                print("Paired soak running; peak RSS bytes: " + ", ".join(
                    f"{name}={result.get('peakResidentBytes', 'unavailable')}"
                    for name, result in manifest["runs"].items()), flush=True)
                next_report = time.monotonic() + 60
            time.sleep(0.5)

        for name in executables:
            metrics_path = root / name / "metrics.json"
            metrics = json.loads(metrics_path.read_text())
            expected = {"commit": args.commit, "clients": args.clients,
                        "simulatedLatencyMilliseconds": args.latency_ms,
                        "simulatedPacketLossPercent": args.packet_loss_percent,
                        "addressSanitizerEnabled": name == "sanitizer"}
            if any(metrics.get(key) != value for key, value in expected.items()):
                raise ValueError(f"{name} metrics do not match the requested candidate/workload")
            if metrics.get("cycles", 0) < args.cycles:
                raise ValueError(f"{name} did not complete the requested cycles")
            if manifest["runs"][name]["elapsedSeconds"] < args.duration_seconds:
                raise ValueError(f"{name} did not complete the requested duration")
            if name == "native":
                growth = metrics.get("residentMemoryGrowthPercentAfterWarmup")
                if (metrics.get("monotonicMemoryGrowthDetected") is not False
                        or not isinstance(growth, (int, float))
                        or not math.isfinite(growth) or growth > 1):
                    raise ValueError("native RSS growth gate did not pass")
                if args.release_gates and (len(metrics.get("residentMemorySamplesBytes", [])) < 20
                                           or metrics.get("initialResidentMemoryBytes", 0) <= 0):
                    raise ValueError("native RSS measurement is unavailable")
            manifest["runs"][name]["metricsSha256"] = sha256(metrics_path)
        manifest["status"] = "passed"
    except (OSError, ValueError, RuntimeError, KeyboardInterrupt) as error:
        failure = str(error) or "paired soak interrupted"
        manifest.update(status="failed", failure=failure)
    finally:
        for process in processes.values():
            if process.poll() is None:
                process.terminate()
        for name, process in processes.items():
            try:
                code = process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                code = process.wait()
            result = manifest["runs"][name]
            result.setdefault("exitCode", code)
            result.setdefault("elapsedSeconds", time.monotonic() - started[name])
            (root / name / "exit-code.txt").write_text(f"{code}\n")
            streams[name].flush()
            result["logSha256"] = sha256(root / name / "soak.log")
        for stream in streams.values():
            stream.close()
        manifest["finishedAt"] = utc_now()
        manifest["memoryConstraintsAtFinish"] = memory_constraints()
        write_manifest(root, manifest)
    if failure:
        print(f"Paired soak failed: {failure}", file=sys.stderr)
        return 1
    print(f"Paired soak passed. Both runs and their verdicts: {root / 'manifest.json'}")
    return 0


def interrupted(_signal, _frame):
    raise KeyboardInterrupt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native-executable", type=Path, required=True)
    parser.add_argument("--sanitizer-executable", type=Path, required=True)
    parser.add_argument("--artifacts-dir", type=Path, required=True)
    for name in ("cycles", "clients", "duration-seconds", "latency-ms", "packet-loss-percent"):
        parser.add_argument("--" + name, type=int, required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--release-gates", action="store_true")
    args = parser.parse_args()
    if (args.cycles < 1 or args.clients < 1 or args.duration_seconds < 0
            or args.latency_ms < 0 or not 0 <= args.packet_loss_percent <= 100):
        parser.error("invalid workload limits")
    if args.release_gates and (args.cycles < 100 or args.clients != 8
                               or args.duration_seconds < 86400 or args.latency_ms == 0
                               or args.packet_loss_percent == 0 or len(args.commit) != 40
                               or any(c not in "0123456789abcdefABCDEF" for c in args.commit)):
        parser.error("release gates require an exact commit and the full eight-client 24-hour workload")
    signal.signal(signal.SIGTERM, interrupted)
    try:
        return run_pair(args)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"Cannot start paired soak: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
