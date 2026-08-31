#!/usr/bin/env python3

"""Generate the source-oriented TES3MP SPDX 2.3 software bill of materials."""

from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys


REPOSITORY = "https://github.com/dev-zetta/TES3MP"


def read_match(path: pathlib.Path, pattern: str, description: str) -> str:
    match = re.search(pattern, path.read_text(encoding="utf-8"))
    if match is None:
        raise RuntimeError(f"could not determine {description} from {path}")
    return match.group(1)


def git_revision(root: pathlib.Path) -> str:
    result = subprocess.run(
        ["git", "-C", str(root), "rev-parse", "HEAD"],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def creation_time() -> str:
    source_date_epoch = os.environ.get("SOURCE_DATE_EPOCH")
    if source_date_epoch is not None:
        timestamp = datetime.datetime.fromtimestamp(
            int(source_date_epoch), tz=datetime.timezone.utc
        )
    else:
        timestamp = datetime.datetime.now(tz=datetime.timezone.utc)
    return timestamp.replace(microsecond=0).isoformat().replace("+00:00", "Z")


def package(
    spdx_id: str,
    name: str,
    version: str | None = None,
    download: str = "NOASSERTION",
    license_declared: str = "NOASSERTION",
    comment: str | None = None,
    purl: str | None = None,
) -> dict[str, object]:
    result: dict[str, object] = {
        "SPDXID": spdx_id,
        "name": name,
        "downloadLocation": download,
        "filesAnalyzed": False,
        "licenseConcluded": "NOASSERTION",
        "licenseDeclared": license_declared,
        "copyrightText": "NOASSERTION",
    }
    if version is not None:
        result["versionInfo"] = version
    if comment is not None:
        result["comment"] = comment
    if purl is not None:
        result["externalRefs"] = [
            {
                "referenceCategory": "PACKAGE-MANAGER",
                "referenceType": "purl",
                "referenceLocator": purl,
            }
        ]
    return result


def generate(root: pathlib.Path) -> dict[str, object]:
    tes3mp_version = read_match(
        root / "components/openmw-mp/Version.hpp",
        r'#define\s+TES3MP_VERSION\s+"([^"]+)"',
        "TES3MP version",
    )
    openmw_version = ".".join(
        read_match(
            root / "CMakeLists.txt",
            rf"set\(OPENMW_VERSION_{part}\s+([0-9]+)\)",
            f"OpenMW {part.lower()} version",
        )
        for part in ("MAJOR", "MINOR", "RELEASE")
    )
    gns_revision = read_match(
        root / "cmake/TES3MPDependencies.cmake",
        r"GIT_TAG\s+([0-9a-f]{40})",
        "GameNetworkingSockets revision",
    )
    revision = git_revision(root)

    packages = [
        package(
            "SPDXRef-Package-TES3MP",
            "TES3MP",
            tes3mp_version,
            f"git+{REPOSITORY}.git@{revision}",
            comment=(
                "License terms are recorded in LICENSE. Specialist review of the "
                "TES3MP additional GPL terms remains a stable-release gate."
            ),
            purl=f"pkg:github/dev-zetta/TES3MP@{revision}",
        ),
        package(
            "SPDXRef-Package-OpenMW",
            "OpenMW",
            openmw_version,
            "https://gitlab.com/OpenMW/openmw",
            "GPL-3.0-or-later",
            purl=f"pkg:generic/openmw@{openmw_version}",
        ),
        package(
            "SPDXRef-Package-GameNetworkingSockets",
            "GameNetworkingSockets",
            gns_revision,
            f"git+https://github.com/ValveSoftware/GameNetworkingSockets.git@{gns_revision}",
            "BSD-3-Clause",
            purl=f"pkg:github/ValveSoftware/GameNetworkingSockets@{gns_revision}",
        ),
        package(
            "SPDXRef-Package-libsodium",
            "libsodium",
            ">=1.0.18",
            "https://github.com/jedisct1/libsodium",
            "ISC",
            purl="pkg:generic/libsodium@1.0.18",
        ),
    ]

    relationships = [
        {
            "spdxElementId": "SPDXRef-DOCUMENT",
            "relationshipType": "DESCRIBES",
            "relatedSpdxElement": "SPDXRef-Package-TES3MP",
        },
        {
            "spdxElementId": "SPDXRef-Package-TES3MP",
            "relationshipType": "VARIANT_OF",
            "relatedSpdxElement": "SPDXRef-Package-OpenMW",
        },
    ]

    known = {"GameNetworkingSockets", "libsodium"}
    for dependency in ("GameNetworkingSockets", "libsodium"):
        relationships.append(
            {
                "spdxElementId": "SPDXRef-Package-TES3MP",
                "relationshipType": "DEPENDS_ON",
                "relatedSpdxElement": f"SPDXRef-Package-{dependency}",
            }
        )

    extern_root = root / "extern"
    for dependency_path in sorted(path for path in extern_root.iterdir() if path.is_dir()):
        if dependency_path.name in known:
            continue
        safe_name = re.sub(r"[^A-Za-z0-9.-]", "-", dependency_path.name)
        spdx_id = f"SPDXRef-Bundled-{safe_name}"
        packages.append(
            package(
                spdx_id,
                dependency_path.name,
                comment=(
                    f"Bundled source under extern/{dependency_path.name}; version and license "
                    "metadata require confirmation against third-party notices."
                ),
            )
        )
        relationships.append(
            {
                "spdxElementId": "SPDXRef-Package-TES3MP",
                "relationshipType": "CONTAINS",
                "relatedSpdxElement": spdx_id,
            }
        )

    namespace_digest = hashlib.sha256(
        f"{revision}:{tes3mp_version}".encode("utf-8")
    ).hexdigest()
    return {
        "spdxVersion": "SPDX-2.3",
        "dataLicense": "CC0-1.0",
        "SPDXID": "SPDXRef-DOCUMENT",
        "name": f"TES3MP-{tes3mp_version}-source",
        "documentNamespace": f"{REPOSITORY}/sbom/{namespace_digest}",
        "creationInfo": {
            "created": creation_time(),
            "creators": ["Tool: TES3MP-CI-generate-spdx-sbom"],
            "comment": (
                "Source inventory for TES3MP additions and bundled extern components. "
                "Release packaging must merge resolved platform and binary dependency metadata."
            ),
        },
        "packages": packages,
        "relationships": relationships,
    }


def validate(document: dict[str, object]) -> None:
    if document.get("spdxVersion") != "SPDX-2.3":
        raise RuntimeError("unexpected SPDX version")
    packages = document.get("packages")
    relationships = document.get("relationships")
    if not isinstance(packages, list) or not isinstance(relationships, list):
        raise RuntimeError("packages and relationships must be arrays")
    identifiers = {entry.get("SPDXID") for entry in packages if isinstance(entry, dict)}
    required = {
        "SPDXRef-Package-TES3MP",
        "SPDXRef-Package-OpenMW",
        "SPDXRef-Package-GameNetworkingSockets",
        "SPDXRef-Package-libsodium",
    }
    if not required.issubset(identifiers):
        raise RuntimeError("required package is absent from the SBOM")
    if len(identifiers) != len(packages):
        raise RuntimeError("package SPDX identifiers are not unique")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output", type=pathlib.Path, required=True, help="SPDX JSON output path"
    )
    parser.add_argument(
        "--root",
        type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parent.parent,
        help="TES3MP source tree",
    )
    args = parser.parse_args()

    document = generate(args.root.resolve())
    validate(document)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(
        f"Wrote {args.output} with {len(document['packages'])} packages",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
