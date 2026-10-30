#!/usr/bin/env python3
"""Read a bounded TES3 index for private, autonomous canonical-game fixtures.

Only identifiers and reference transforms are returned. Game assets are neither
copied nor embedded, and importing this module performs no I/O.
"""
from __future__ import annotations

import hashlib
import math
from pathlib import Path
import struct

MAX_FILE_BYTES = 512 * 1024 * 1024
MAX_RECORD_BYTES = 32 * 1024 * 1024
MAX_SUBRECORD_BYTES = 8 * 1024 * 1024
MAX_RECORDS = 500_000
MAX_REFERENCES = 1_000_000
CENSUS = 'Seyda Neen, Census and Excise Office'
CELL_SIZE = 8192


def _text(value: bytes) -> str:
    value = value.split(b'\0', 1)[0]
    if len(value) > 4096:
        raise ValueError('TES3 identifier exceeds the fixture limit')
    result = value.decode('cp1252')
    if any(ord(c) < 32 or ord(c) == 127 for c in result):
        raise ValueError('TES3 identifier contains a control character')
    return result


def _subrecords(payload: bytes):
    offset = 0
    while offset < len(payload):
        if len(payload) - offset < 8:
            raise ValueError('Truncated TES3 subrecord header')
        kind, size = struct.unpack_from('<4sI', payload, offset)
        offset += 8
        if size > MAX_SUBRECORD_BYTES or size > len(payload) - offset:
            raise ValueError('Invalid TES3 subrecord size')
        yield kind, payload[offset:offset + size]
        offset += size


def _transform(value: bytes) -> tuple[list[float], list[float]]:
    if len(value) != 24:
        raise ValueError('Invalid TES3 reference transform size')
    values = struct.unpack('<6f', value)
    if not all(math.isfinite(x) and abs(x) < 100_000_000 for x in values):
        raise ValueError('Invalid TES3 reference transform')
    return list(values[:3]), list(values[3:])


def _cell(payload: bytes) -> tuple[str, list[dict]]:
    name = ''
    description = None
    current = None
    references = []

    def finish():
        if current and not current.get('deleted') and current.get('refId') and 'position' in current:
            references.append(current.copy())

    for kind, value in _subrecords(payload):
        if kind == b'FRMR':
            finish()
            if len(value) != 4:
                raise ValueError('Invalid TES3 reference identity')
            current = {'refNum': struct.unpack('<I', value)[0], 'mpNum': 0}
        elif current is None:
            if kind == b'NAME':
                name = _text(value)
            elif kind == b'DATA':
                if len(value) != 12:
                    raise ValueError('Invalid TES3 cell data')
                flags, x, y = struct.unpack('<Iii', value)
                description = name if flags & 1 else f'{x}, {y}'
        elif kind == b'NAME':
            current['refId'] = _text(value)
        elif kind == b'DATA':
            current['position'], current['rotation'] = _transform(value)
        elif kind == b'DODT':
            position, rotation = _transform(value)
            current['destination'] = {'position': position, 'rotation': rotation}
        elif kind == b'DNAM':
            current['destinationCell'] = _text(value)
        elif kind == b'DELE':
            current['deleted'] = True
        elif kind == b'FLTV':
            if len(value) != 4:
                raise ValueError('Invalid TES3 lock level')
            current['lockLevel'] = struct.unpack('<i', value)[0]
        elif kind == b'TNAM':
            current['trap'] = _text(value)
    finish()
    if description is None:
        raise ValueError('TES3 CELL lacks its cell data')
    for reference in references:
        reference['cell'] = description
        if 'destination' in reference:
            destination = reference['destination']
            destination['cell'] = reference.pop('destinationCell', '') or (
                f'{math.floor(destination["position"][0] / CELL_SIZE)}, '
                f'{math.floor(destination["position"][1] / CELL_SIZE)}')
    return description, references


def read_index(path: Path) -> tuple[dict[str, str], dict[str, list[dict]]]:
    """Index relevant types and Census/Seyda Neen references with strict bounds."""
    path = Path(path)
    size = path.stat().st_size
    if size < 16 or size > MAX_FILE_BYTES:
        raise ValueError('TES3 file size is outside the fixture limit')
    kinds: dict[str, str] = {}
    cells: dict[str, list[dict]] = {}
    reference_count = 0
    with path.open('rb') as stream:
        for index in range(MAX_RECORDS):
            header = stream.read(16)
            if not header:
                return kinds, cells
            if len(header) != 16:
                raise ValueError('Truncated TES3 record header')
            kind, length, _, _ = struct.unpack('<4sIII', header)
            if index == 0 and kind != b'TES3':
                raise ValueError('Fixture content is not a TES3 file')
            if length > MAX_RECORD_BYTES or length > size - stream.tell():
                raise ValueError('Invalid TES3 record size')
            if kind not in (b'TES3', b'CELL', b'CONT', b'DOOR', b'NPC_'):
                stream.seek(length, 1)
                continue
            payload = stream.read(length)
            if kind == b'CELL':
                description, refs = _cell(payload)
                reference_count += len(refs)
                if reference_count > MAX_REFERENCES:
                    raise ValueError('TES3 references exceed the fixture limit')
                nearby = {f'{x}, {y}' for x in range(-3, 0) for y in range(-10, -7)}
                if description.casefold() == CENSUS.casefold() or description in nearby:
                    if description in cells:
                        raise ValueError('Duplicate selected TES3 cell')
                    cells[description] = refs
            elif kind != b'TES3':
                for subkind, value in _subrecords(payload):
                    if subkind == b'NAME':
                        kinds[_text(value).casefold()] = kind.decode('ascii')
                        break
    raise ValueError('TES3 record count exceeds the fixture limit')


def _approach(reference: dict, distance: float = 80) -> dict:
    position = reference['position'].copy()
    yaw = reference['rotation'][2]
    position[0] -= math.sin(yaw) * distance
    position[1] -= math.cos(yaw) * distance
    position[2] += 16
    return {'cell': reference['cell'], 'position': position, 'rotation': [0.0, yaw]}


def build_fixture(data_dir: Path) -> dict:
    """Select existing canonical references for a disposable local game test."""
    root = Path(data_dir)
    matches = [p for p in root.iterdir() if p.is_file() and p.name.casefold() == 'morrowind.esm']
    if len(matches) != 1:
        raise ValueError('Exactly one Morrowind.esm is required')
    path = matches[0]
    kinds, cells = read_index(path)
    census = next((name for name in cells if name.casefold() == CENSUS.casefold()), None)
    if census is None:
        raise ValueError('Canonical Census Office cell was not found')
    references = cells[census]
    doors = [r for r in references if kinds.get(r['refId'].casefold()) == 'DOOR'
             and 'destination' in r and r['destination']['cell'] != census]
    exterior_doors = [r for r in doors if ',' in r['destination']['cell']
                      and _is_exterior(r['destination']['cell'])]
    if not exterior_doors:
        raise ValueError('Canonical Census Office exterior teleport door was not found')
    door = min(exterior_doors, key=lambda r: r['refNum']).copy()
    containers = [r for r in references if kinds.get(r['refId'].casefold()) == 'CONT'
                  and r.get('lockLevel', 0) <= 0 and not r.get('trap')]
    if not containers:
        raise ValueError('Canonical Census Office container was not found')
    container = min(containers, key=lambda r: r['refNum']).copy()
    # Socucius Ergalla's canonical internal identifier is "chargen class".
    # Sellus Gravius is a friendly fallback; avoid the office's guard.
    dialogue_priority = {'chargen class': 0, 'socucius ergalla': 0,
                         'chargen captain': 1, 'sellus gravius': 1}
    dialogue_npcs = [r for r in references if kinds.get(r['refId'].casefold()) == 'NPC_'
                     and r['refId'].casefold() in dialogue_priority]
    unique_npcs = [r for r in dialogue_npcs
                   if sum(n['refId'].casefold() == r['refId'].casefold() for n in dialogue_npcs) == 1
                   and sum(n['refNum'] == r['refNum'] for n in references) == 1]
    if not unique_npcs:
        raise ValueError('Unique friendly Census Office dialogue NPC was not found')
    dialogue = min(unique_npcs, key=lambda r: (dialogue_priority[r['refId'].casefold()], r['refNum'])).copy()
    guards = [r for refs in cells.values() for r in refs
              if _is_exterior(r['cell']) and kinds.get(r['refId'].casefold()) == 'NPC_'
              and 'guard' in r['refId'].casefold() and 'chargen' not in r['refId'].casefold()]
    if not guards:
        raise ValueError('Canonical Seyda Neen guard was not found')
    landing = door['destination']['position']
    guard = min(guards, key=lambda r: sum((a-b)**2 for a,b in zip(r['position'], landing))).copy()
    for reference in (door, container, guard, dialogue):
        reference['approach'] = _approach(reference)
        if reference['refNum'] == 0:
            raise ValueError('Canonical fixture reference has no identity')
    # Door model origins can sit above their walkable floor. Keep the player's
    # feet on the known Census spawn floor, within server grounding tolerances.
    door['approach']['position'][2] = 193.0
    # Conversational NPCs can stand with their backs against a wall. Place the
    # player in front of the actor, then face back toward it; the generic rear
    # approach can put the player outside the room's collision geometry.
    dialogue['approach'] = _approach(dialogue, -80)
    dialogue['approach']['rotation'][1] = (dialogue['rotation'][2] + math.pi) % (2 * math.pi)
    with path.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    return {'schemaVersion': 1, 'sourceEsm': {'name': path.name, 'size': path.stat().st_size, 'sha256': digest},
            'spawn': {'cell': census, 'position': [1130.3388671875, -387.14947509766, 193.0],
                      'rotation': [0.09375, 1.5078122615814]},
            'door': door, 'container': container, 'combatTarget': guard, 'dialogueTarget': dialogue,
            'weakness': {'spellId': 'mwnet_gameplay_weakness', 'effectId': 31,
                         'effectRefId': 'WeaknessToMagicka', 'magnitude': 20, 'duration': 120}}


def _is_exterior(value: str) -> bool:
    parts = value.split(', ')
    return len(parts) == 2 and all(p.lstrip('-').isdigit() for p in parts)
