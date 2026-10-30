#!/usr/bin/env python3
"""Synthetic TES3 fixture-reader regressions; no proprietary game data required."""
import hashlib
import importlib.util
import math
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('gameplay_content', Path(__file__).with_name('gameplay_content.py'))
CONTENT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CONTENT)


def sub(kind, payload):
    return kind.encode() + struct.pack('<I', len(payload)) + payload


def record(kind, payload):
    return kind.encode() + struct.pack('<III', len(payload), 0, 0) + payload


def name(value):
    return sub('NAME', value.encode('cp1252') + b'\0')


def reference(number, ref_id, position=(0, 0, 0), destination=None, lock=0, trap='', rotation=(0, 0, 0)):
    result = sub('FRMR', struct.pack('<I', number)) + name(ref_id)
    result += sub('DATA', struct.pack('<6f', *position, *rotation))
    if destination:
        result += sub('DODT', struct.pack('<6f', *destination, 0, 0, 0))
    if lock:
        result += sub('FLTV', struct.pack('<i', lock))
    if trap:
        result += sub('TNAM', trap.encode() + b'\0')
    return result


def cell(description, references, exterior=None):
    flags, x, y = (0, *exterior) if exterior else (1, 0, 0)
    return record('CELL', name(description) + sub('DATA', struct.pack('<Iii', flags, x, y)) + references)


def fixture_bytes(dialogue_rotation=(0, 0, 0)):
    return (record('TES3', b'') + record('DOOR', name('fixture_exit'))
            + record('CONT', name('fixture_container'))
            + record('NPC_', name('Imperial Guard')) + record('NPC_', name('chargen guard'))
            + record('NPC_', name('chargen captain')) + record('NPC_', name('chargen class'))
            + cell(CONTENT.CENSUS,
                   reference(1, 'fixture_exit', position=(0, 0, 306.8), destination=(-9877, -72420, 200))
                   + reference(2, 'fixture_container', lock=20)
                   + reference(3, 'fixture_container', position=(10, 20, 30))
                   + reference(4, 'fixture_container', trap='fire damage')
                   + reference(7, 'chargen captain') + reference(8, 'chargen class', position=(40, 50, 60),
                                                               rotation=dialogue_rotation))
            + cell('Seyda Neen', reference(5, 'chargen guard', position=(-9877, -72420, 200))
                   + reference(6, 'Imperial Guard', position=(-9900, -72500, 150)), (-2, -9)))


class GameplayContentTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.esm = self.root / 'mOrRoWiNd.EsM'
        self.esm.write_bytes(fixture_bytes())

    def test_canonical_selection_and_no_asset_copy(self):
        before = set(self.root.iterdir())
        result = CONTENT.build_fixture(self.root)
        self.assertEqual(result['schemaVersion'], 1)
        self.assertEqual(result['door']['refNum'], 1)
        self.assertEqual(result['door']['destination']['cell'], '-2, -9')
        self.assertEqual(result['container']['refNum'], 3)
        self.assertEqual(result['combatTarget']['refNum'], 6)
        self.assertEqual(result['combatTarget']['mpNum'], 0)
        self.assertEqual(result['combatTarget']['cell'], '-2, -9')
        self.assertEqual(result['dialogueTarget']['refId'], 'chargen class')
        self.assertEqual(result['dialogueTarget']['refNum'], 8)
        self.assertEqual(result['dialogueTarget']['approach']['cell'], CONTENT.CENSUS)
        self.assertEqual(result['sourceEsm']['sha256'], hashlib.sha256(self.esm.read_bytes()).hexdigest())
        self.assertEqual(result['weakness']['effectId'], 31)
        self.assertEqual(set(self.root.iterdir()), before)

    def test_dialogue_target_requires_unique_friendly_reference(self):
        self.esm.write_bytes(fixture_bytes().replace(name('chargen class'), name('unknown npc__')))
        self.assertEqual(CONTENT.build_fixture(self.root)['dialogueTarget']['refNum'], 7)
        duplicate = sub('NAME', b'chargen class\0\0\0')
        self.esm.write_bytes(fixture_bytes().replace(name('chargen captain'), duplicate))
        with self.assertRaisesRegex(ValueError, 'dialogue NPC'):
            CONTENT.build_fixture(self.root)

    def test_dialogue_approach_stands_in_front_and_faces_actor(self):
        for yaw in (0, math.pi / 2, math.pi, 3 * math.pi / 2):
            with self.subTest(yaw=yaw):
                self.esm.write_bytes(fixture_bytes((0, 0, yaw)))
                fixture = CONTENT.build_fixture(self.root)
                target = fixture['dialogueTarget']
                approach = target['approach']
                dx = approach['position'][0] - target['position'][0]
                dy = approach['position'][1] - target['position'][1]
                self.assertGreater(dx * math.sin(yaw) + dy * math.cos(yaw), 79.9)
                player_yaw = approach['rotation'][1]
                self.assertGreater(-dx * math.sin(player_yaw) - dy * math.cos(player_yaw), 79.9)
                self.assertAlmostEqual(math.hypot(dx, dy), 80)
                self.assertGreaterEqual(player_yaw, 0)
                self.assertLess(player_yaw, 2 * math.pi)
                self.assertEqual(approach['cell'], CONTENT.CENSUS)
                self.assertEqual(fixture['door']['approach']['position'], [0, -80, 193])
                self.assertEqual(fixture['container']['approach']['position'], [10, -60, 46])

    def test_door_pivot_height_does_not_exceed_grounding_budget(self):
        door = CONTENT.build_fixture(self.root)['door']
        feet_z = door['approach']['position'][2]
        census_floor_z = 193
        self.assertLessEqual(abs(feet_z - census_floor_z), 128)
        # The old model-origin +16 approach fell 129.8 units before touching
        # this floor, exceeding server correction authorization by 1.8 units.
        self.assertGreater(door['position'][2] + 16 - census_floor_z, 128)

    def test_record_bounds_and_truncation(self):
        for data in (b'bad', b'TES3' + struct.pack('<III', 20, 0, 0), fixture_bytes() + b'CELL'):
            with self.subTest(data=data[:16]):
                self.esm.write_bytes(data)
                with self.assertRaises(ValueError):
                    CONTENT.read_index(self.esm)

    def test_subrecord_bounds_and_transform(self):
        malformed = (b'NAME' + struct.pack('<I', 200),
                     name(CONTENT.CENSUS) + sub('DATA', b'bad'),
                     name(CONTENT.CENSUS) + sub('DATA', struct.pack('<Iii', 1, 0, 0))
                     + reference(1, 'fixture_exit', position=(float('nan'), 0, 0)))
        for payload in malformed:
            self.esm.write_bytes(record('TES3', b'') + record('CELL', payload))
            with self.subTest(payload=payload[:12]), self.assertRaises(ValueError):
                CONTENT.read_index(self.esm)

    def test_duplicate_selected_cell_and_identity(self):
        self.esm.write_bytes(fixture_bytes() + cell(CONTENT.CENSUS, b''))
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            CONTENT.build_fixture(self.root)
        data = fixture_bytes().replace(sub('FRMR', struct.pack('<I', 1)), sub('FRMR', struct.pack('<I', 0)), 1)
        self.esm.write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'identity'):
            CONTENT.build_fixture(self.root)

    def test_missing_required_world_and_ambiguous_file(self):
        self.esm.write_bytes(record('TES3', b''))
        with self.assertRaisesRegex(ValueError, 'Census'):
            CONTENT.build_fixture(self.root)
        (self.root / 'Morrowind.esm').write_bytes(fixture_bytes())
        with self.assertRaisesRegex(ValueError, 'Exactly one'):
            CONTENT.build_fixture(self.root)

    def test_resource_limits(self):
        with patch.object(CONTENT, 'MAX_FILE_BYTES', 16), self.assertRaises(ValueError):
            CONTENT.read_index(self.esm)
        with patch.object(CONTENT, 'MAX_RECORDS', 1), self.assertRaisesRegex(ValueError, 'record count'):
            CONTENT.read_index(self.esm)
        with patch.object(CONTENT, 'MAX_REFERENCES', 1), self.assertRaisesRegex(ValueError, 'references'):
            CONTENT.read_index(self.esm)
        with patch.object(CONTENT, 'MAX_SUBRECORD_BYTES', 1), self.assertRaises(ValueError):
            CONTENT.read_index(self.esm)


if __name__ == '__main__':
    unittest.main()
