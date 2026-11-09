import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'docker/discovery/host-diagnostics'))
import no_sleep as profile


class NoSleepTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / 'etc/systemd/system').mkdir(parents=True)
        self.state = self.root / 'state'

    def apply(self, verifier=lambda: {'status': 'test-verified'}):
        return profile.apply(self.root, self.state, verifier=verifier, reload_manager=lambda: None)

    def test_failed_live_verification_restores_every_new_file_and_mask(self):
        def fail():
            raise ValueError('injected verification failure')
        with self.assertRaisesRegex(ValueError, 'injected'):
            self.apply(fail)
        self.assertTrue(all(not p.exists() for p, _, _ in profile.paths(self.root)))
        self.assertTrue(all(not (self.root / 'etc/systemd/system' / u).is_symlink() for u in profile.TARGETS))

    def test_existing_masks_are_preserved_after_failed_apply(self):
        (self.root / 'etc/systemd/system/sleep.target').symlink_to('/dev/null')
        with self.assertRaises(ValueError):
            self.apply(lambda: (_ for _ in ()).throw(ValueError('fail')))
        self.assertEqual(os.readlink(self.root / 'etc/systemd/system/sleep.target'), '/dev/null')
        self.assertFalse((self.root / 'etc/systemd/system/suspend.target').is_symlink())

    def test_regular_or_unexpected_symlink_overrides_are_never_replaced(self):
        target = self.root / 'etc/systemd/system/sleep.target'
        target.write_text('independent unit')
        with self.assertRaisesRegex(ValueError, 'Preserve'):
            self.apply()
        target.unlink()
        target.symlink_to('/independent/unit')
        with self.assertRaisesRegex(ValueError, 'Preserve'):
            self.apply()

    def test_independent_policy_and_later_changes_block_restore(self):
        path = profile.paths(self.root)[0][0]
        path.parent.mkdir(parents=True)
        path.write_text('independent policy')
        with self.assertRaisesRegex(ValueError, 'independently'):
            self.apply()
        path.unlink()
        result = self.apply()
        backup = Path(result['backup'])
        transaction = profile.FileTransaction(backup)
        transaction.entries = json.loads((backup / 'files.json').read_text())
        path.write_text('later independent edit')
        with self.assertRaisesRegex(ValueError, 'independent'):
            profile.restore(transaction, json.loads((backup / 'masks.json').read_text()), self.root)
        self.assertTrue((self.root / 'etc/systemd/system/sleep.target').is_symlink())

    def test_success_is_idempotent_and_never_restarts_services(self):
        result = self.apply()
        self.assertEqual(result['status'], 'test-verified')
        self.assertTrue(all(os.readlink(self.root / 'etc/systemd/system' / u) == '/dev/null' for u in profile.TARGETS))
        again = self.apply()
        self.assertTrue(again['alreadyApplied'])
        self.assertEqual(len(list((self.state / 'backups').iterdir())), 1)

    def test_live_verification_requires_all_masks_and_all_four_capabilities(self):
        def call(args):
            output = 'LoadState=masked\nUnitFileState=masked\n' if args[0] == 'systemctl' else 's "no"\n'
            return mock.Mock(stdout=output)
        with mock.patch.object(profile, 'command', side_effect=call) as command:
            self.assertEqual(profile.verify()['status'], 'sleep-disabled')
            self.assertEqual(command.call_count, 9)
            self.assertFalse(any('suspend' in c.args[0] for c in command.call_args_list))

    def test_live_challenge_is_availability_and_cannot_pass_as_disabled(self):
        def call(args):
            return mock.Mock(stdout='LoadState=masked\nUnitFileState=masked\n' if args[0] == 'systemctl' else 's "challenge"\n')
        with mock.patch.object(profile, 'command', side_effect=call):
            with self.assertRaisesRegex(ValueError, 'still offers'):
                profile.verify()


if __name__ == '__main__':
    unittest.main()
