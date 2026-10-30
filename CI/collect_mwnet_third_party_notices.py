#!/usr/bin/env python3
"""Collect verbatim third-party notice inputs with hashes and source provenance.

This helper does not infer license compatibility, redistributable rights, or
corresponding-source completeness. SDK roots must be the actual build inputs.
An SDK's unshipped development dependencies can be included conservatively;
--sdk-component restricts collection when a complete binary graph is available.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


SOURCE_NOTICES = {
    "Base64": ("extern/Base64/Base64.h", "block"),
    "PicoSHA2": ("extern/PicoSHA2/picosha2.h", "block"),
    "oics": ("extern/oics/ICSChannel.cpp", "block"),
    "TinyXML": ("extern/oics/tinyxml/tinyxml.h", "block"),
    "osg-ffmpeg-videoplayer": ("extern/osg-ffmpeg-videoplayer/License.txt", "whole"),
    "sol3": ("extern/sol3/sol/version.hpp", "prefix"),
    "MurmurHash3": ("extern/smhasher/MurmurHash3.cpp", "prefix"),
    "CoreScripts": ("files/mwnet/core-scripts/LICENSE", "whole"),
    "dkjson": ("files/mwnet/core-scripts/lib/lua/dkjson.lua", "lua-long"),
    "DejaVu": ("files/data/fonts/DejaVuFontLicense.txt", "whole"),
    "DemonicLetters": ("files/data/fonts/DemonicLettersFontLicense.txt", "whole"),
    "MysticCards": ("files/data/fonts/MysticCardsFontLicense.txt", "whole"),
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def source_notice(data: bytes, mode: str) -> bytes:
    if mode == "lua-long":
        match = re.search(rb"--\[(=*)\[(.*?)\]\1\]", data, re.DOTALL)
        if match is None:
            raise ValueError("Expected an initial Lua long-comment notice")
        return match.group(0) + b"\n"
    if mode == "block":
        match = re.search(rb"/\*.*?\*/", data, re.DOTALL)
        if match is None:
            raise ValueError("Expected an initial license comment")
        return match.group(0) + b"\n"
    if mode == "prefix":
        # The notice precedes the first include/guard in these source files.
        return data[:data.index(b"#")].rstrip() + b"\n"
    return data


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--source-ref", required=True)
    parser.add_argument("--sdk-root", type=Path, action="append", default=[])
    parser.add_argument("--sdk-component", action="append", default=[])
    parser.add_argument("--extra-text", action="append", default=[], metavar="LABEL=PATH")
    parser.add_argument("--static-manifest", type=Path,
                        default=Path(__file__).resolve().parent.parent / "docs/license/third-party/manifest.json")
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    if args.output_dir.exists():
        parser.error("--output-dir must be new; preserve earlier notice evidence")
    revision = subprocess.check_output(["git", "-C", str(args.source_dir), "rev-parse", args.source_ref + "^{commit}"], text=True).strip()
    inputs = []
    texts = []

    def add(label: str, origin: str, original: bytes, notice: bytes):
        inputs.append({"label": label, "origin": origin, "inputSha256": sha256(original), "noticeSha256": sha256(notice)})
        texts.append("=" * 78 + "\n" + label + "\nSource: " + origin + "\n" + "=" * 78 + "\n\n" + notice.decode("utf-8", errors="replace").rstrip() + "\n")

    for label, (path, mode) in SOURCE_NOTICES.items():
        data = subprocess.check_output(["git", "-C", str(args.source_dir), "show", revision + ":" + path])
        add(label, "git:" + revision + ":" + path, data, source_notice(data, mode))
    static_manifest = json.loads(args.static_manifest.read_text())
    if static_manifest.get("schemaVersion") != 1:
        raise ValueError("Unknown static notice manifest schema")
    for entry in static_manifest["entries"]:
        source = subprocess.check_output(["git", "-C", str(args.source_dir), "show", revision + ":" + entry["sourcePinFile"]])
        if entry["sourcePin"].encode() not in source:
            raise ValueError("Static notice pin differs from runtime source: " + entry["name"])
        archive_sha512 = entry.get("sourceArchiveSha512")
        if archive_sha512 is not None and ("SHA512=" + archive_sha512).encode() not in source:
            raise ValueError("Static notice archive pin differs from runtime source: " + entry["name"])
        data = (args.static_manifest.parent / entry["licenseFile"]).read_bytes()
        if sha256(data) != entry["sha256"]:
            raise ValueError("Static notice bytes differ from reviewed input: " + entry["name"])
        add(entry["name"], entry["upstreamNotice"], data, data)
        inputs[-1]["sourceArchive"] = entry["upstreamSourceArchive"]
        if archive_sha512 is not None:
            inputs[-1]["sourceArchiveSha512"] = archive_sha512
    for root in args.sdk_root:
        for path in sorted(root.rglob("copyright")):
            if path.parent.parent.name != "share":
                continue
            name = path.parent.name
            if args.sdk_component and name not in args.sdk_component:
                continue
            data = path.read_bytes()
            add("SDK " + name, str(path.resolve()), data, data)
    for argument in args.extra_text:
        label, path = argument.split("=", 1)
        data = Path(path).read_bytes()
        add(label, str(Path(path).resolve()), data, data)
    args.output_dir.mkdir(parents=True)
    notice = ("MWNet third-party copyright and license notices\n\n" +
              "These notices preserve their individual upstream terms. The MWNet/OpenMW/TES3MP\n" +
              "LICENSE is supplied separately. This collection does not relicense components.\n\n" +
              "\n".join(texts)).encode("utf-8")
    (args.output_dir / "THIRD_PARTY_NOTICES.txt").write_bytes(notice)
    (args.output_dir / "FETCHED_SOURCES.txt").write_text(
        "Pinned fetched component source archives (see runtime build scripts for options):\n\n" +
        "\n".join(entry["name"] + "\n" + entry["upstreamSourceArchive"] + "\n"
                    for entry in static_manifest["entries"]))
    (args.output_dir / "notice-inputs.json").write_text(json.dumps({
        "schemaVersion": 1, "runtimeSourceCommit": revision,
        "scope": "Collected notice inputs; no compatibility, source-delivery or rights acceptance is implied.",
        "noticeSha256": sha256(notice), "inputs": inputs,
    }, indent=2) + "\n")
    print(f"Collected {len(inputs)} notice inputs; {len(notice)} bytes")


if __name__ == "__main__":
    main()
