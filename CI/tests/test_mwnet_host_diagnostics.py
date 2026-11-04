#!/usr/bin/env python3
"""Host diagnostic durability, retention and reversible configuration safeguards."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'CI'))
from mwnet_temp import prepare_temp_root


def module(name):
    path = ROOT / 'docker/discovery/host-diagnostics' / (name + '.py')
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


COLLECTOR, SETUP = module('telemetry'), module('configure')


class DiagnosticsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir=prepare_temp_root())
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name)
        self.folder.chmod(0o700)

    def ring(self, maximum=1024, files=3):
        result = COLLECTOR.DurableRing(self.folder, maximum, files)
        self.addCleanup(result.finish)
        return result

    def test_rotation_is_bounded_and_keeps_newest_complete_rows(self):
        ring = self.ring()
        for number in range(35):
            ring.write({'sequence': number, 'padding': 'x' * 250})
        ring.close()
        paths = sorted(self.folder.glob('host*.jsonl'))
        self.assertEqual(len(paths), 3)
        self.assertTrue(all(path.stat().st_size <= 1024 for path in paths))
        rows = [json.loads(line) for path in paths for line in path.read_text().splitlines()]
        self.assertEqual(max(row['sequence'] for row in rows), 34)
        self.assertGreater(min(row['sequence'] for row in rows), 0)

    def test_every_observation_is_synchronized_and_files_private(self):
        ring = self.ring()
        ring.open()
        with mock.patch.object(COLLECTOR.os, 'fsync', wraps=os.fsync) as sync:
            ring.write({'sequence': 1})
            self.assertEqual(sync.call_count, 1)
        self.assertEqual((self.folder / 'host.jsonl').stat().st_mode & 0o777, 0o600)

    def test_symlink_cannot_redirect_host_observations(self):
        victim = self.folder / 'victim'
        victim.write_text('preserve')
        (self.folder / 'host.jsonl').symlink_to(victim)
        with self.assertRaises(OSError):
            self.ring().write({'sequence': 1})
        self.assertEqual(victim.read_text(), 'preserve')

    def test_hard_link_cannot_redirect_host_observations(self):
        victim = self.folder / 'victim'
        victim.write_text('preserve')
        os.link(victim, self.folder / 'host.jsonl')
        with self.assertRaises(ValueError):
            self.ring().write({'sequence': 1})
        self.assertEqual(victim.read_text(), 'preserve')

    def test_second_logger_is_rejected_across_rotations(self):
        ring = self.ring()
        ring.write({'sequence': 1})
        with self.assertRaises(BlockingIOError):
            COLLECTOR.DurableRing(self.folder)

    def test_torn_row_is_preserved_and_next_row_separated(self):
        path = self.folder / 'host.jsonl'
        path.write_bytes(b'{"partial":')
        ring = self.ring()
        ring.write({'sequence': 1})
        lines = path.read_text().splitlines()
        self.assertEqual(lines[0], '{"partial":')
        self.assertEqual(json.loads(lines[1]), {'sequence': 1})

    def test_nonfinite_and_oversized_observations_are_rejected(self):
        ring = self.ring()
        for value in ({'value': float('nan')}, {'value': 'x' * 2048}):
            with self.assertRaises(ValueError):
                ring.write(value)

    def test_latest_status_is_atomic_and_survives_an_old_temporary_file(self):
        path = self.folder / 'latest.json'
        orphan = self.folder / 'latest.json.old.new'
        orphan.write_text('old partial observation')
        COLLECTOR.write_status(path, {'sequence': 1})
        COLLECTOR.write_status(path, {'sequence': 2})
        self.assertEqual(json.loads(path.read_text()), {'sequence': 2})
        self.assertEqual(path.stat().st_mode & 0o777, 0o640)
        self.assertEqual(orphan.read_text(), 'old partial observation')

    def test_real_host_snapshot_and_one_shot_produce_parseable_durable_data(self):
        value = COLLECTOR.snapshot(disk=self.folder)
        self.assertGreater(value['memoryBytes']['MemAvailable'], 0)
        self.assertGreater(value['uptimeSeconds'], 0)
        self.assertNotIn('commandLine', value)
        result = subprocess.run([sys.executable, '-B', str(Path(COLLECTOR.__file__)),
                                 '--once', '--directory', str(self.folder)], capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        saved = json.loads((self.folder / 'host.jsonl').read_text())
        self.assertEqual(saved['bootId'], value['bootId'])

    def test_ram_backed_output_is_rejected(self):
        with mock.patch.object(COLLECTOR.subprocess, 'check_output', return_value='tmpfs\n'):
            with self.assertRaisesRegex(ValueError, 'persistent disk'):
                COLLECTOR.DurableRing(self.folder)

    def test_rollback_restores_old_bytes_modes_and_removes_only_new_config(self):
        original, added = self.folder / 'original', self.folder / 'added'
        original.write_bytes(b'original bytes'); original.chmod(0o640)
        changes = [(original, b'new content', 0o644), (added, b'new file', 0o644)]
        transaction = SETUP.FileTransaction(self.folder / 'backup')
        transaction.prepare(changes)
        for path, data, mode in changes:
            SETUP.atomic_file(path, data, mode)
        transaction.restore()
        self.assertEqual(original.read_bytes(), b'original bytes')
        self.assertEqual(original.stat().st_mode & 0o777, 0o640)
        self.assertFalse(added.exists())
        self.assertTrue((self.folder / 'backup/files.json').exists())

    def test_independent_edit_blocks_entire_restore_before_any_mutation(self):
        first, second = self.folder / 'first', self.folder / 'second'
        first.write_bytes(b'original1'); second.write_bytes(b'original2')
        changes = [(first, b'new1', 0o644), (second, b'new2', 0o644)]
        transaction = SETUP.FileTransaction(self.folder / 'backup')
        transaction.prepare(changes)
        for path, data, mode in changes:SETUP.atomic_file(path, data, mode)
        first.write_bytes(b'independent edit')
        with self.assertRaises(ValueError):transaction.restore()
        self.assertEqual(second.read_bytes(), b'new2')
        self.assertEqual(first.read_bytes(), b'independent edit')

    def test_altered_backup_blocks_restore_before_any_mutation(self):
        original = self.folder / 'original';original.write_bytes(b'original')
        transaction = SETUP.FileTransaction(self.folder / 'backup')
        transaction.prepare([(original, b'new', 0o644)])
        SETUP.atomic_file(original, b'new')
        (self.folder / 'backup/0').write_bytes(b'altered backup')
        with self.assertRaises(ValueError):transaction.restore()
        self.assertEqual(original.read_bytes(), b'new')

    def test_kdump_path_change_preserves_other_settings_and_rejects_ambiguity(self):
        text = 'USE_KDUMP=1\nKDUMP_COREDIR="/var/crash"\nKDUMP_KERNEL=/var/lib/kdump/vmlinuz\n'
        changed = SETUP.patch_kdump(text)
        self.assertIn('KDUMP_KERNEL=/var/lib/kdump/vmlinuz', changed)
        self.assertIn('KDUMP_COREDIR="/var/crash/mwnet-host"', changed)
        self.assertIn('KDUMP_NUM_DUMPS=0', changed)
        with self.assertRaises(ValueError):SETUP.patch_kdump(text + 'KDUMP_COREDIR="/other"\n')
        with self.assertRaises(ValueError):SETUP.patch_kdump(text + 'SSH=remote\n')

    def test_unexpected_package_changes_are_rejected_before_installation(self):
        result = subprocess.CompletedProcess([], 0, 'Inst nvme-cli (2.16-1 Ubuntu)\nRemv unrelated-package\n', '')
        with mock.patch.object(SETUP.shutil, 'which', return_value=None), \
             mock.patch.object(SETUP, 'command', return_value=result) as run:
            with self.assertRaises(ValueError):SETUP.install_nvme_tool()
            self.assertEqual(run.call_count, 1)

    def test_only_simulated_diagnostic_package_versions_are_installed(self):
        plan = 'Inst nvme-cli (2.16-1 Ubuntu)\nInst rfkill (2.41.3 Ubuntu)\n'
        with mock.patch.object(SETUP.shutil, 'which', return_value=None), \
             mock.patch.object(SETUP, 'command', return_value=subprocess.CompletedProcess([],0,plan,'')) as run:
            SETUP.install_nvme_tool(True)
        self.assertEqual(run.call_args_list[1], mock.call(
            ['apt-get','-y','--no-install-recommends','--no-upgrade','install','nvme-cli=2.16-1','rfkill=2.41.3'], timeout=300))

    def test_verification_failure_restores_files_and_runtime_sysctls(self):
        original, added = self.folder / 'existing.conf', self.folder / 'new.conf'
        original.write_bytes(b'original config')
        changes = [(original, b'new config', 0o644), (added, b'new file', 0o644)]
        real_read = Path.read_text
        def read(path, *args, **kwargs):
            if str(path) == '/sys/class/dmi/id/product_name':return 'NUC8i5BEH\n'
            if str(path) == '/sys/kernel/kexec_crash_loaded':return '1\n'
            return real_read(path, *args, **kwargs)
        def run(args, *unused, **kwargs):
            text = 'inactive\n' if args[:2] == ['systemctl', 'show'] else ''
            code = 1 if len(args) > 1 and args[1] in ('is-enabled', 'is-active') else 0
            return subprocess.CompletedProcess(args, code, text, '')
        def directory(path, *unused):path.mkdir(parents=True, exist_ok=True)
        previous = dict.fromkeys(SETUP.SYSCTL, '0')
        with mock.patch.object(SETUP, 'STATE', self.folder / 'state'), \
             mock.patch.object(SETUP, 'LOGS', self.folder / 'logs'), \
             mock.patch.object(SETUP, 'CORES', self.folder / 'cores'), \
             mock.patch.object(SETUP, 'STATUS', self.folder / 'status'), \
             mock.patch.object(SETUP, 'manifest_verified', return_value={'sourceCommit': '1' * 40}), \
             mock.patch.object(SETUP, 'configuration_changes', return_value=changes), \
             mock.patch.object(SETUP, 'private_directory', side_effect=directory), \
             mock.patch.object(SETUP, 'install_nvme_tool'), \
             mock.patch.object(SETUP.shutil, 'disk_usage', return_value=mock.Mock(free=32 * 1024 ** 3)), \
             mock.patch.object(SETUP.pwd, 'getpwnam', return_value=mock.Mock(pw_uid=1000, pw_gid=1000)), \
             mock.patch.object(SETUP, 'command', side_effect=run), \
             mock.patch.object(SETUP, 'sysctls', return_value=previous), \
             mock.patch.object(SETUP, 'set_sysctls') as settings, \
             mock.patch.object(SETUP, 'verify', side_effect=ValueError('injected verification failure')), \
             mock.patch.object(Path, 'read_text', new=read), mock.patch.dict(os.environ):
            with self.assertRaisesRegex(ValueError, 'injected verification failure'):
                SETUP.apply(self.folder / 'bundle')
        self.assertEqual(original.read_bytes(), b'original config')
        self.assertFalse(added.exists())
        self.assertEqual(settings.call_args_list, [mock.call(SETUP.SYSCTL), mock.call(previous)])

    def test_wifi_block_requires_wired_route_and_no_wireless_addresses(self):
        for route, addresses in (([{'dev':'wlp0s20f3'}], []),
                                 ([{'dev':'eno1'}], [{'local':'192.0.2.1'}])):
            answers = [subprocess.CompletedProcess([], 0, json.dumps(route), ''),
                       subprocess.CompletedProcess([], 0, json.dumps([{'addr_info':addresses}]), '')]
            with mock.patch.object(SETUP, 'command', side_effect=answers), \
                 mock.patch.object(Path, 'read_text', return_value='1\n'), \
                 mock.patch.object(SETUP, 'wifi_radio') as radio:
                with self.assertRaises(ValueError):SETUP.wifi_guard()
                radio.assert_not_called()

    def test_wifi_block_verifies_runtime_and_saved_state(self):
        radio = {'id':'1', 'name':'phy0', 'device':'/pci/0000:00:14.3/phy0', 'soft':'1'}
        with mock.patch.object(SETUP, 'wifi_radio', return_value=radio), \
             mock.patch.object(SETUP, 'command') as run, \
             mock.patch.object(Path, 'exists', return_value=True), \
             mock.patch.object(Path, 'read_text', return_value='1\n'):
            SETUP.set_wifi(radio, '1')
        self.assertEqual(run.call_args_list, [mock.call(['systemctl','start','systemd-rfkill.socket']),
                                             mock.call(['rfkill','block','1'])])

    def test_changed_wireless_hardware_is_not_mutated(self):
        saved = {'id':'1', 'name':'phy0', 'device':'/original', 'soft':'0'}
        with mock.patch.object(SETUP, 'wifi_radio', return_value=saved | {'device':'/different'}), \
             mock.patch.object(SETUP, 'command') as run:
            with self.assertRaises(ValueError):SETUP.set_wifi(saved, '1')
            run.assert_not_called()


if __name__ == '__main__':
    unittest.main()
