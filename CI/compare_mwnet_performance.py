#!/usr/bin/env python3
"""Compare two MWNet headless metrics artifacts against the release threshold."""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any


PROFILE_FIELDS = (
    "clients",
    "simulatedLatencyMilliseconds",
    "simulatedPacketLossPercent",
)
DIRECT_METRICS = (
    "tickP99Microseconds",
    "serializationP99Microseconds",
    "residentMemoryBytes",
)
NORMALIZED_METRICS = (
    "inboundBytes",
    "outboundBytes",
)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare MWNet headless performance metrics"
    )
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--threshold-percent", type=float, default=5.0)
    parser.add_argument(
        "--allow-regression-with-justification",
        type=Path,
        help="Non-empty reviewed justification that explicitly accepts a regression",
    )
    parser.add_argument("--report", type=Path, help="Write a JSON comparison report")
    return parser.parse_args()


def load(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"failed to read {path}: {error}") from error
    if not isinstance(value, dict) or value.get("schemaVersion") != 1:
        raise ValueError(f"{path} is not a MWNet metrics schemaVersion 1 object")
    return value


def number(document: dict[str, Any], field: str, path: Path) -> float:
    value = document.get(field)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{path}: {field} must be numeric")
    result = float(value)
    if not math.isfinite(result) or result < 0:
        raise ValueError(f"{path}: {field} must be finite and non-negative")
    return result


def normalized_traffic(document: dict[str, Any], field: str, path: Path) -> float:
    cycles = number(document, "cycles", path)
    clients = number(document, "clients", path)
    if cycles <= 0 or clients <= 0:
        raise ValueError(f"{path}: cycles and clients must be positive")
    return number(document, field, path) / cycles / clients


def compare(name: str, baseline: float, candidate: float,
            threshold_percent: float) -> dict[str, Any]:
    if baseline == 0:
        change_percent: float | None = 0.0 if candidate == 0 else None
        passed = candidate == 0
    else:
        change_percent = (candidate - baseline) / baseline * 100.0
        passed = change_percent <= threshold_percent
    return {
        "metric": name,
        "baseline": baseline,
        "candidate": candidate,
        "changePercent": change_percent,
        "passed": passed,
    }


def main() -> int:
    arguments = parse_arguments()
    if not math.isfinite(arguments.threshold_percent) or arguments.threshold_percent < 0:
        print("--threshold-percent must be finite and non-negative", file=sys.stderr)
        return 2

    try:
        baseline = load(arguments.baseline)
        candidate = load(arguments.candidate)
        for field in PROFILE_FIELDS:
            if baseline.get(field) != candidate.get(field):
                raise ValueError(
                    f"profile mismatch for {field}: "
                    f"{baseline.get(field)!r} != {candidate.get(field)!r}"
                )

        comparisons: list[dict[str, Any]] = []
        for field in DIRECT_METRICS:
            comparisons.append(compare(
                field,
                number(baseline, field, arguments.baseline),
                number(candidate, field, arguments.candidate),
                arguments.threshold_percent,
            ))
        for field in NORMALIZED_METRICS:
            comparisons.append(compare(
                f"{field}PerClientCycle",
                normalized_traffic(baseline, field, arguments.baseline),
                normalized_traffic(candidate, field, arguments.candidate),
                arguments.threshold_percent,
            ))
    except ValueError as error:
        print(error, file=sys.stderr)
        return 2

    regressions = [entry for entry in comparisons if not entry["passed"]]
    justification = arguments.allow_regression_with_justification
    accepted = False
    if regressions and justification is not None:
        try:
            accepted = bool(justification.read_text(encoding="utf-8").strip())
        except OSError as error:
            print(f"failed to read justification: {error}", file=sys.stderr)
            return 2
        if not accepted:
            print("regression justification must be non-empty", file=sys.stderr)
            return 2

    report = {
        "schemaVersion": 1,
        "baselineCommit": baseline.get("commit", "unknown"),
        "candidateCommit": candidate.get("commit", "unknown"),
        "thresholdPercent": arguments.threshold_percent,
        "profile": {field: baseline[field] for field in PROFILE_FIELDS},
        "comparisons": comparisons,
        "regressionsAcceptedByJustification": bool(regressions and accepted),
        "passed": not regressions or accepted,
    }
    rendered = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if arguments.report:
        arguments.report.parent.mkdir(parents=True, exist_ok=True)
        arguments.report.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
