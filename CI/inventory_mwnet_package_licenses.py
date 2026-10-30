#!/usr/bin/env python3
"""Inventory delivered notice, font and binary files without executing a package.

The JSON is factual evidence, not a license-compatibility or source-completeness
decision. ZIP/tar readers use the Python standard library; DMG/APFS needs 7z.
No archive is extracted to the filesystem, and no binaries are executed.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import struct
import subprocess
import tarfile
import zipfile


TEXT_SUFFIXES = {".txt", ".md", ".lua", ".cfg", ".json", ".xml", ".html", ".cmake"}
MARKERS = {
    "Base64-tomykaira": "2016 tomykaira",
    "PicoSHA2-okdshin": "2017 okdshin",
    "oics-Alberto-Salguero": "2011 Alberto G. Salguero",
    "sol-Rapptz-ThePhD": "Rapptz, ThePhD and contributors",
    "osg-ffmpeg-scrawl-Robinson": "2014 scrawl, Chris Robinson",
    "GNS-Valve": "2018, Valve Corporation",
    "yaml-cpp-Jesse-Beder": "2008-2015 Jesse Beder",
    "Recast-Mikko-Mononen": "2009 Mikko Mononen",
    "dkjson-David-Kolf": "2010-2013 David Heiko Kolf",
    "MyGUI-Developers": "2007-2013 MyGUI Developers",
    "LGPL-title": "GNU LESSER GENERAL PUBLIC LICENSE",
    "OpenSSL": "OpenSSL",
}


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def notice_name(name: str) -> bool:
    base = PurePosixPath(name).name.casefold()
    return any(word in base for word in ("license", "licence", "copying", "copyright", "notice", "authors", "credits"))


def text_name(name: str) -> bool:
    p = PurePosixPath(name)
    return notice_name(name) or p.suffix.casefold() in TEXT_SUFFIXES or p.name.casefold().startswith("readme")


def binary_name(name: str) -> bool:
    p = PurePosixPath(name)
    if p.name in {"mwnet", "mwnet-server", "mwnet-directory"}:
        return True
    if p.suffix.casefold() in {".dll", ".dylib", ".exe", ".so"} or ".so." in p.name:
        return True
    return "/MacOS/" in name or bool(re.search(r"/([^/]+)\.framework/Versions/[^/]+/\1$", name))


def linked_libraries(data: bytes) -> dict:
    """Read normal loader imports; do not infer static or runtime-loaded code."""
    if data.startswith(b"\x7fELF") and data[4:6] == b"\x02\x01":
        phoff = struct.unpack_from("<Q", data, 32)[0]
        phsize, phnum = struct.unpack_from("<HH", data, 54)
        segments = [struct.unpack_from("<IIQQQQQQ", data, phoff + i * phsize) for i in range(phnum)]
        dynamic = next((s for s in segments if s[0] == 2), None)
        if not dynamic:
            return {"format": "ELF64", "libraries": []}
        tags = []
        for offset in range(dynamic[2], dynamic[2] + dynamic[5], 16):
            tag, value = struct.unpack_from("<qQ", data, offset)
            if tag == 0:
                break
            tags.append((tag, value))
        strings = next((value for tag, value in tags if tag == 5), None)
        segment = next(s for s in segments if strings is not None and s[0] == 1 and s[3] <= strings < s[3] + s[5])
        base = segment[2] + strings - segment[3]
        libs = [data[base + v:data.index(b"\0", base + v)].decode() for tag, v in tags if tag == 1]
        return {"format": "ELF64", "libraries": sorted(set(libs))}
    if data[:4] in {b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe"}:
        slices = [0]
        if data[:4] == b"\xca\xfe\xba\xbe":
            count = struct.unpack_from(">I", data, 4)[0]
            slices = [struct.unpack_from(">IIIII", data, 8 + i * 20)[2] for i in range(count)]
        libs = []
        for base in slices:
            if data[base:base + 4] != b"\xcf\xfa\xed\xfe":
                continue
            count = struct.unpack_from("<I", data, base + 16)[0]
            offset = base + 32
            for _ in range(count):
                command, size = struct.unpack_from("<II", data, offset)
                if size < 8:
                    raise ValueError("Invalid Mach-O load command size")
                if command in {0xc, 0x80000018, 0x8000001f, 0x80000023}:
                    start = offset + struct.unpack_from("<I", data, offset + 8)[0]
                    libs.append(data[start:data.index(b"\0", start)].decode())
                offset += size
        return {"format": "Mach-O", "libraries": sorted(set(libs))}
    if data.startswith(b"MZ"):
        pe = struct.unpack_from("<I", data, 0x3c)[0]
        if data[pe:pe + 4] != b"PE\0\0":
            raise ValueError("Invalid PE header")
        section_count = struct.unpack_from("<H", data, pe + 6)[0]
        optional_size = struct.unpack_from("<H", data, pe + 20)[0]
        optional = pe + 24
        magic = struct.unpack_from("<H", data, optional)[0]
        directory = optional + (112 if magic == 0x20b else 96)
        rva = struct.unpack_from("<I", data, directory + 8)[0]
        sections = [struct.unpack_from("<8sIIIIIIHHI", data, optional + optional_size + i * 40)
                    for i in range(section_count)]

        def file_offset(address):
            for section in sections:
                if section[2] <= address < section[2] + max(section[1], section[3]):
                    return section[4] + address - section[2]
            raise ValueError("Unmapped PE RVA")

        libs = []
        if rva:
            offset = file_offset(rva)
            while any(data[offset:offset + 20]):
                name_rva = struct.unpack_from("<I", data, offset + 12)[0]
                start = file_offset(name_rva)
                libs.append(data[start:data.index(b"\0", start)].decode())
                offset += 20
        return {"format": "PE", "libraries": sorted(set(libs)), "scope": "Normal imports; delay-load imports are not included."}
    return {"format": "unrecognized", "libraries": []}


class Archive:
    def __init__(self, path: Path):
        self.path = path
        self.handle = None
        if path.suffix.casefold() == ".zip":
            self.kind = "zip"
            self.handle = zipfile.ZipFile(path)
            self.entries = [{"path": e.filename, "bytes": e.file_size, "kind": "file"}
                            for e in self.handle.infolist() if not e.is_dir()]
        elif path.suffix.casefold() == ".dmg":
            self.kind = "dmg-7z"
            result = subprocess.run(["7z", "l", "-slt", str(path)], check=True, capture_output=True, text=True)
            self.entries = []
            for block in result.stdout.split("\n\n"):
                entry = dict(line.split(" = ", 1) for line in block.splitlines() if " = " in line)
                mode = entry.get("Mode", "")
                if mode.startswith(("-", "l")):
                    self.entries.append({"path": entry["Path"], "bytes": int(entry.get("Size") or 0),
                                         "kind": "symlink" if mode.startswith("l") else "file"})
        else:
            self.kind = "tar"
            self.handle = tarfile.open(path)
            self.entries = [{"path": e.name, "bytes": e.size,
                             "kind": "file" if e.isfile() else "symlink"}
                            for e in self.handle.getmembers() if e.isfile() or e.issym()]
        names = [e["path"] for e in self.entries]
        if len(names) != len(set(names)):
            raise ValueError("Duplicate archive member names are ambiguous")

    def read(self, name: str) -> bytes:
        if self.kind == "zip":
            return self.handle.read(name)
        if self.kind == "tar":
            with self.handle.extractfile(name) as stream:
                return stream.read()
        return subprocess.run(["7z", "x", "-so", str(self.path), name], check=True, capture_output=True).stdout

    def close(self):
        if self.handle is not None:
            self.handle.close()


def inventory(path: Path) -> dict:
    archive = Archive(path)
    result = {"archive": str(path.resolve()), "bytes": path.stat().st_size, "sha256": file_digest(path),
              "reader": archive.kind, "files": archive.entries, "noticeFiles": [], "fontFiles": [],
              "binaryFiles": [], "textFilesInspected": [], "noticeMarkerMatches": {k: [] for k in MARKERS},
              "embeddedBuildIdentities": [], "loaderImports": []}
    try:
        for entry in archive.entries:
            name = entry["path"]
            if entry["kind"] != "file":
                continue
            binary = binary_name(name)
            if binary:
                result["binaryFiles"].append(name)
            font = PurePosixPath(name).suffix.casefold() in {".ttf", ".otf"}
            text = text_name(name) and entry["bytes"] <= 2 * 1024 * 1024
            ffmpeg = bool(re.search(r"/(?:lib)?avutil[-.].*\.(?:dll|dylib)$", name))
            if not (font or text or binary):
                continue
            data = archive.read(name)
            if binary:
                result["loaderImports"].append({"path": name, "sha256": digest(data), **linked_libraries(data)})
            if font:
                result["fontFiles"].append({**entry, "sha256": digest(data)})
            if text and b"\0" not in data:
                result["textFilesInspected"].append(name)
                if notice_name(name):
                    result["noticeFiles"].append({**entry, "sha256": digest(data)})
                for key, marker in MARKERS.items():
                    if marker.casefold().encode() in data.lower():
                        result["noticeMarkerMatches"][key].append(name)
            if ffmpeg:
                strings = re.findall(rb"[ -~]{8,}", data)
                identities = [s.decode("ascii") for s in strings
                              if s.startswith((b"FFmpeg version ", b"--prefix="))]
                result["embeddedBuildIdentities"].append({"path": name, "sha256": digest(data), "strings": identities})
    finally:
        archive.close()
    result["counts"] = {"regularFiles": sum(e["kind"] == "file" for e in archive.entries),
                        "symlinks": sum(e["kind"] == "symlink" for e in archive.entries),
                        "binaries": len(result["binaryFiles"]), "noticeFiles": len(result["noticeFiles"]),
                        "fonts": len(result["fontFiles"])}
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", action="append", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--require-compiled-notices", action="store_true",
                        help="Fail when the known compiled source component notices are absent (not legal clearance)")
    args = parser.parse_args()
    result = {"schemaVersion": 1, "scope": "Delivered file and notice evidence only; no legal acceptance is implied.",
              "toolSha256": file_digest(Path(__file__)), "packages": [inventory(p) for p in args.archive]}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    for package in result["packages"]:
        print(Path(package["archive"]).name, json.dumps(package["counts"], sort_keys=True))
    if args.require_compiled_notices:
        missing = [(Path(p["archive"]).name, key) for p in result["packages"]
                   for key, matches in p["noticeMarkerMatches"].items()
                   if key not in {"LGPL-title", "OpenSSL"} and not matches]
        if missing:
            raise SystemExit("Missing compiled component notice markers: " + repr(missing))


if __name__ == "__main__":
    main()
