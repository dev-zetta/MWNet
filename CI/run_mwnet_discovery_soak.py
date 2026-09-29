#!/usr/bin/env python3
"""Bounded local HTTPS discovery churn, process restart and resource evidence.

Caddy must already serve --origin with the supplied trusted CA, proxying local
port 8080. Only the local directory process started here is stopped/restarted.
Artifacts and SQLite live in --artifacts, which must be on persistent disk.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import time
import urllib.request


def resources(pid):
    result = {}
    try:
        for line in Path(f"/proc/{pid}/status").read_text().splitlines():
            if line.startswith(("VmRSS:", "VmHWM:")):
                key, value, _ = line.split()
                result[key[:-1] + "Bytes"] = int(value) * 1024
        fields = Path(f"/proc/{pid}/stat").read_text().split(")", 1)[1].split()
        result["cpuSeconds"] = (int(fields[11]) + int(fields[12])) / os.sysconf("SC_CLK_TCK")
        result["fds"] = len(list(Path(f"/proc/{pid}/fd").iterdir()))
    except (FileNotFoundError, ProcessLookupError):
        result["exited"] = True
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", required=True)
    parser.add_argument("--tests", required=True)
    parser.add_argument("--origin", default="https://localhost:8443")
    parser.add_argument("--ca", required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--revision", default="uncommitted")
    parser.add_argument("--seconds", type=int, default=7200)
    args = parser.parse_args()
    if args.seconds < 20:
        parser.error("duration must allow at least 20 seconds for outage recovery")
    if not Path(args.ca).is_file() or not os.access(args.ca, os.R_OK):
        parser.error("the local CA certificate must exist and be readable by this process")
    args.artifacts.mkdir(parents=True, exist_ok=True)
    database = args.artifacts / "directory.sqlite3"
    if database.exists():
        parser.error("use a fresh artifacts directory to preserve previous evidence")
    command = [args.directory, "--origin", args.origin, "--database", str(database), "--local-test"]
    environment = dict(os.environ, TMPDIR=str(args.artifacts.resolve()))
    hashes = {name: hashlib.sha256(Path(path).read_bytes()).hexdigest()
              for name, path in (("directory", args.directory), ("workload", args.tests))}
    start = time.monotonic()
    result = {"requestedSeconds": args.seconds, "directoryCommand": command,
              "startedUnix": time.time(), "completed": False, "errors": [],
              "revision": args.revision, "binarySha256": hashes}
    directory = workload = None
    max_rss = 0
    exit_codes = []
    try:
        with (args.artifacts / "directory.log").open("w") as server_log, \
             (args.artifacts / "workload.log").open("w") as work_log, \
             (args.artifacts / "samples.jsonl").open("w") as samples:
            directory = subprocess.Popen(command, stdout=server_log, stderr=subprocess.STDOUT, env=environment)
            time.sleep(1)
            if directory.poll() is not None:
                raise RuntimeError("directory failed to start")
            workload = subprocess.Popen([args.tests, "--https", args.origin, args.ca, str(args.seconds)],
                                        stdout=work_log, stderr=subprocess.STDOUT, env=environment)
            outage_at = start + min(300, args.seconds / 3)
            outage_duration = min(30, args.seconds / 6)
            restart_at = None
            interrupted = False
            while workload.poll() is None:
                now = time.monotonic()
                if now - start > args.seconds + 60:
                    raise RuntimeError("workload exceeded its deadline")
                if not interrupted and now >= outage_at:
                    directory.terminate()
                    exit_codes.append(directory.wait(timeout=10))
                    restart_at = time.monotonic() + outage_duration
                    interrupted = True
                if restart_at and now >= restart_at:
                    if hashlib.sha256(Path(args.directory).read_bytes()).hexdigest() != hashes["directory"]:
                        raise RuntimeError("directory binary changed during the workload")
                    directory = subprocess.Popen(command, stdout=server_log, stderr=subprocess.STDOUT, env=environment)
                    restart_at = None
                    time.sleep(1)
                sample = {"elapsedSeconds": now-start, "workload": resources(workload.pid)}
                if restart_at is None:
                    if directory.poll() is not None:
                        raise RuntimeError("directory exited unexpectedly")
                    sample["directory"] = resources(directory.pid)
                    max_rss = max(max_rss, sample["directory"].get("VmRSSBytes", 0))
                    with urllib.request.urlopen("http://127.0.0.1:8080/metrics", timeout=3) as response:
                        sample["metrics"] = json.load(response)
                    if int(sample["metrics"]["pending"]) > 2 or max_rss > 256 * 1024 * 1024:
                        raise RuntimeError("directory resource gate exceeded")
                sample["databaseBytes"] = sum(p.stat().st_size for p in args.artifacts.glob("directory.sqlite3*"))
                samples.write(json.dumps(sample) + "\n")
                samples.flush()
                time.sleep(2)
            result["workloadExitCode"] = workload.returncode
            if workload.returncode != 0:
                raise RuntimeError("HTTPS workload failed")
            # Rates live for 60 seconds; challenges for 30. Check cleanup after both expire.
            time.sleep(65)
            with urllib.request.urlopen("http://127.0.0.1:8080/metrics", timeout=3) as response:
                result["finalMetrics"] = json.load(response)
            for field in ("listings", "visible", "pending", "challenges", "rateBuckets", "listingBytes"):
                if int(result["finalMetrics"][field]) != 0:
                    raise RuntimeError(f"retained state after cleanup: {field}")
            with sqlite3.connect(database) as db:
                if db.execute("SELECT count(*) FROM listings").fetchone()[0] != 0:
                    raise RuntimeError("expired SQLite listing rows retained")
                if db.execute("PRAGMA integrity_check").fetchone()[0] != "ok":
                    raise RuntimeError("SQLite integrity check failed")
            result["completed"] = True
    except KeyboardInterrupt:
        result["errors"].append("interrupted by operator before gate completion")
    except Exception as error:
        result["errors"].append(str(error))
    finally:
        for process in (workload, directory):
            if process is not None and process.poll() is None:
                process.terminate()
                try:
                    code = process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    code = process.wait()
                if process is directory:
                    exit_codes.append(code)
        result.update(directoryExitCodes=exit_codes, elapsedSeconds=time.monotonic()-start,
                      peakDirectoryRssBytes=max_rss, finishedUnix=time.time())
        if any(code != 0 for code in exit_codes):
            result["completed"] = False
            result["errors"].append("directory shutdown failed")
        (args.artifacts / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    return 0 if result["completed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
