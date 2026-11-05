#!/usr/bin/env python3
"""Map delivered Mach-O sections to preserved SDK inputs without executing them.

The comparison includes file-backed sections, zero-fill metadata and section
relocations. Load commands, symbol/export tables, signatures and other LINKEDIT
data are reported or excluded, so a match is not whole-binary equivalence or
complete source/distribution acceptance.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import struct
import subprocess

from inventory_mwnet_package_licenses import Archive, binary_name, file_digest
from mwnet_temp import configure_temp_environment


def region(data, offset, size):
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise ValueError('Mach-O region exceeds its slice')
    return data[offset:offset + size]


def unpack(fmt, data, offset):
    return struct.unpack(fmt, region(data, offset, struct.calcsize(fmt)))


def name(raw):
    return raw.split(b'\0', 1)[0].decode('ascii')


def version(raw):
    return f'{raw >> 16}.{(raw >> 8) & 255}.{raw & 255}'


def thin_payload(data):
    header = unpack('<8I', data, 0)
    magic, cpu, subtype, filetype, commands, command_bytes, flags, _ = header
    if magic != 0xfeedfacf or commands > 4096:
        raise ValueError('Expected a bounded little-endian 64-bit Mach-O slice')
    region(data, 32, command_bytes)
    limit = 32 + command_bytes
    offset = 32
    sections, libraries, minimum = [], [], []
    install_name = None
    for _ in range(commands):
        command, size = unpack('<II', data, offset)
        if size < 8 or size % 8 or offset + size > limit:
            raise ValueError('Invalid Mach-O load command bounds')
        payload = region(data, offset, size)
        if command == 0x19:  # LC_SEGMENT_64
            if size < 72:
                raise ValueError('Truncated Mach-O segment')
            segment = unpack('<II16s4Q4I', payload, 0)
            segment_name, count = name(segment[2]), segment[9]
            if count > 4096 or 72 + count * 80 != size:
                raise ValueError('Invalid Mach-O section count')
            for index in range(count):
                values = unpack('<16s16sQQ8I', payload, 72 + index * 80)
                section_name, declared_segment = name(values[0]), name(values[1])
                address, length, file_offset, alignment, reloc_offset, reloc_count, section_flags = values[2:9]
                if declared_segment != segment_name or segment_name == '__LINKEDIT':
                    raise ValueError('Unexpected Mach-O section segment')
                zero_fill = section_flags & 255 in {1, 12, 18}
                if length and not zero_fill and file_offset < limit:
                    raise ValueError('Mach-O section overlaps load commands')
                content = b'' if zero_fill else region(data, file_offset, length)
                relocations = region(data, reloc_offset, reloc_count * 8)
                sections.append({'segment': segment_name, 'section': section_name,
                                 'address': address, 'bytes': length, 'alignment': alignment,
                                 'flags': section_flags, 'zeroFill': zero_fill,
                                 'sha256': None if zero_fill else hashlib.sha256(content).hexdigest(),
                                 'relocations': reloc_count,
                                 'relocationsSha256': hashlib.sha256(relocations).hexdigest()})
        elif command in {0xc, 0xd, 0x80000018, 0x8000001f, 0x80000023}:
            if size < 24:
                raise ValueError('Truncated Mach-O library command')
            start, _, current, compatible = unpack('<4I', payload, 8)
            if not 24 <= start < size or b'\0' not in payload[start:]:
                raise ValueError('Invalid Mach-O library name')
            row = {'name': name(payload[start:]), 'currentVersion': version(current),
                   'compatibilityVersion': version(compatible), 'command': hex(command)}
            if command == 0xd:
                install_name = row
            else:
                libraries.append(row)
        elif command == 0x32:  # LC_BUILD_VERSION
            platform, minos, sdk, tools = unpack('<4I', payload, 8)
            if size != 24 + tools * 8:
                raise ValueError('Invalid Mach-O build-version command')
            minimum.append({'platform': platform, 'minimum': version(minos), 'sdk': version(sdk)})
        elif command == 0x24:  # LC_VERSION_MIN_MACOSX
            minos, sdk = unpack('<2I', payload, 8)
            minimum.append({'platform': 1, 'minimum': version(minos), 'sdk': version(sdk)})
        offset += size
    if offset != limit:
        raise ValueError('Incomplete Mach-O commands')
    identity = {'cpu': cpu, 'subtype': subtype, 'filetype': filetype,
                'headerFlags': flags, 'sections': sections}
    comparable = any(s['bytes'] and not s['zeroFill'] for s in sections)
    # Abseil ships valid empty compatibility libraries. Their loader/export
    # metadata is still inventoried, but empty payloads must never match each
    # other as evidence of a source/provider identity.
    fingerprint = hashlib.sha256(json.dumps(identity, sort_keys=True, separators=(',', ':')).encode()).hexdigest() if comparable else None
    return dict(identity, sectionPayloadSha256=fingerprint, sectionPayloadComparable=comparable, installName=install_name,
                linkedLibraries=libraries, minimumOsVersions=minimum)


def macho_payloads(data):
    if data[:4] == b'\xcf\xfa\xed\xfe':
        return [thin_payload(data)]
    if data[:4] not in {b'\xca\xfe\xba\xbe', b'\xca\xfe\xba\xbf'}:
        raise ValueError('Unsupported Mach-O magic')
    count, = unpack('>I', data, 4)
    if not 1 <= count <= 16:
        raise ValueError('Invalid universal Mach-O architecture count')
    wide = data[:4] == b'\xca\xfe\xba\xbf'
    fmt, stride = ('>IIQQII', 32) if wide else ('>5I', 20)
    result, spans, architectures = [], [], set()
    region(data, 8, count * stride)
    for index in range(count):
        cpu, subtype, offset, size, alignment, *_ = unpack(fmt, data, 8 + index * stride)
        if alignment > 63 or offset < 8 + count * stride or offset % (1 << alignment):
            raise ValueError('Invalid universal Mach-O slice alignment')
        if any(offset < end and start < offset + size for start, end in spans):
            raise ValueError('Overlapping universal Mach-O slices')
        if (cpu, subtype) in architectures:
            raise ValueError('Duplicate universal Mach-O architecture')
        row = thin_payload(region(data, offset, size))
        if (row['cpu'], row['subtype']) != (cpu, subtype):
            raise ValueError('Universal Mach-O header/slice architecture mismatch')
        spans.append((offset, offset + size))
        architectures.add((cpu, subtype))
        result.append(row)
    return result


def sdk_member_paths(listing):
    paths = []
    for block in listing.split('\n\n'):
        row = dict(line.split(' = ', 1) for line in block.splitlines() if ' = ' in line)
        path = row.get('Path', '')
        attributes = row.get('Attributes', '').split()
        if (not re.search(r'(^|/)installed/[^/]+/lib/.*\.(dylib|so)$', path)
                or not attributes or not attributes[-1].startswith('-')):
            continue
        parts = PurePosixPath(path)
        if parts.is_absolute() or '..' in parts.parts or any(c in path for c in '\\:*?[]\r\n'):
            raise ValueError('Unsafe SDK archive member')
        paths.append(path)
    if not paths or len(paths) != len(set(paths)):
        raise ValueError('Empty or ambiguous SDK library inventory')
    return paths


def extract_sdk_libraries(archive, output):
    process = subprocess.run(['7z', 'l', '-slt', str(archive)], check=True,
                             capture_output=True, text=True, timeout=60)
    paths = sdk_member_paths(process.stdout)
    output.mkdir()
    process = subprocess.run(['7z', 'x', '-y', '-bd', '-o' + str(output), str(archive), *paths],
                             capture_output=True, text=True, timeout=120)
    (output.parent / 'sdk-extraction.log').write_text(process.stdout + process.stderr)
    if process.returncode:
        raise ValueError('SDK regular-file extraction failed: ' + process.stderr[-2000:])
    result = []
    for path in paths:
        file = output / path
        resolved = file.resolve(strict=True)
        if not resolved.is_relative_to(output.resolve()):
            raise ValueError('SDK library resolves outside extraction directory')
        data = file.read_bytes()
        # Only regular archive entries are selected. Alias/chain symlinks do
        # not need to be materialized to compare the actual provider payloads.
        if data[:4] not in {b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe', b'\xca\xfe\xba\xbf'}:
            raise ValueError('Selected SDK library is not Mach-O: ' + path)
        result.append({'path': path, 'sha256': hashlib.sha256(data).hexdigest(),
                       'slices': macho_payloads(data)})
    return result


def inspect(package, sdk, output):
    output.mkdir(parents=True)
    report = {'status': 'running',
              'toolSha256': file_digest(Path(__file__)),
              'scope': 'Exact Mach-O section payload/metadata and relocation matches only; excludes load commands and LINKEDIT symbol/export/signature data; not full binary equivalence or distribution/gameplay acceptance',
              'package': {'name': package.name, 'sha256': file_digest(package)},
              'sdk': {'name': sdk.name, 'sha256': file_digest(sdk)},
              'sdkLibraries': [], 'packageBinaries': [], 'fullDependencyCoverageAccepted': False}
    try:
        report['sdkLibraries'] = extract_sdk_libraries(sdk, output / 'sdk-libraries')
        index = {}
        for library in report['sdkLibraries']:
            for slice in library['slices']:
                if slice['sectionPayloadComparable']:
                    index.setdefault(slice['sectionPayloadSha256'], []).append({'path': library['path'], 'sha256': library['sha256']})
        archive = Archive(package)
        try:
            for entry in archive.entries:
                if entry['kind'] != 'file' or not binary_name(entry['path']):
                    continue
                data = archive.read(entry['path'])
                slices = macho_payloads(data)
                for slice in slices:
                    slice['sdkSectionPayloadMatches'] = index.get(slice['sectionPayloadSha256'], [])
                report['packageBinaries'].append({'path': entry['path'], 'sha256': hashlib.sha256(data).hexdigest(), 'slices': slices})
        finally:
            archive.close()
        report['status'] = 'inspected'
    except Exception as error:
        report['status'] = 'failed'
        report['error'] = type(error).__name__ + ': ' + str(error)
        raise
    finally:
        with (output / 'payload-comparison.json').open('x') as stream:
            stream.write(json.dumps(report, indent=2) + '\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', required=True, type=Path)
    parser.add_argument('--package-sha256', required=True)
    parser.add_argument('--sdk', required=True, type=Path)
    parser.add_argument('--sdk-sha256', required=True)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    configure_temp_environment()
    if args.output.exists():
        parser.error('Refusing to overwrite preserved payload evidence')
    for path, expected in [(args.package, args.package_sha256), (args.sdk, args.sdk_sha256)]:
        if not re.fullmatch('[0-9a-f]{64}', expected) or file_digest(path) != expected:
            parser.error('Archive SHA256 mismatch: ' + path.name)
    result = inspect(args.package, args.sdk, args.output)
    print(json.dumps({'status': result['status'], 'sdkLibraries': len(result['sdkLibraries']),
                      'packageBinaries': len(result['packageBinaries'])}))


if __name__ == '__main__':
    main()
