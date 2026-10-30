#!/usr/bin/env python3
"""Run native, private recovery acceptance; never targets a live deployment."""

import argparse
import datetime
import json
import os
from pathlib import Path
import re
import shutil
import socket
import sqlite3
import subprocess
import tarfile
import time
import urllib.request

from mwnet_recovery_snapshot import digest, inventory, restore, snapshot


LUA_PROBE = r'''
customEventHooks.registerHandler("OnServerPostInit", function()
    local function equal(a, b)
        if type(a) ~= type(b) then return false end
        if type(a) ~= "table" then return a == b end
        for k, v in pairs(a) do if not equal(v, b[k]) then return false end end
        for k, _ in pairs(b) do if a[k] == nil then return false end end
        return true
    end
    if os.getenv("MWNET_RECOVERY_CREATE_OLD") == "1" then
        local p = Player(0, "MWNetRecoveryOld")
        p.data.login.name = p.accountName
        p.data.character.race = "dark elf"
        p.data.stats.healthBase = 173
        p.data.stats.healthCurrent = 127
        p.data.attributes.Strength.base = 61
        p.data.skills.Acrobatics.base = 47
        p.data.inventory = {{refId = "gold_001", count = 19, charge = -1, enchantmentCharge = -1}}
        p.data.customVariables.recovery = "old-runtime-synthetic-profile"
        p:CreateAccount()
        assert(p:HasAccount(), "Old native CoreScripts did not save profile")
        WorldInstance.data.customVariables.recovery = "old-runtime-world"
        WorldInstance:SaveToDrive()
        mwnet.LogMessage(enumerations.log.INFO, "MWNET_RECOVERY_OLD_CREATED")
    else
        local file = assert(io.open(os.getenv("MWNET_RECOVERY_EXPECTED"), "rb"))
        local expected = assert(require("dkjson").decode(file:read("*a")))
        file:close()
        tableHelper.fixNumericalKeys(expected)
        assert(equal(WorldInstance.data, expected.world), "Restored world differs after native CoreScripts load")
        local count = 0
        for name, data in pairs(expected.players) do
            local p = Player(0, name)
            assert(p:HasAccount(), "Restored profile not found")
            p:LoadFromDrive()
            assert(equal(p.data, data), "Restored character differs after native CoreScripts load")
            count = count + 1
        end
        assert(count > 0, "No character profiles exercised")
        mwnet.LogMessage(enumerations.log.INFO, "MWNET_RECOVERY_READBACK_OK")
    end
    mwnet.StopServer(0)
end)
'''


def private_write(path, data):
    path = Path(path)
    path.write_text(data)
    path.chmod(0o600)


def port(kind=socket.SOCK_STREAM):
    with socket.socket(socket.AF_INET, kind) as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


class Acceptance:
    def __init__(self, args):
        self.args = args
        self.root = args.output.resolve()
        self.root.mkdir(mode=0o700, parents=True, exist_ok=False)
        (self.root / 'tmp').mkdir(mode=0o700)
        self.env = dict(os.environ, TMPDIR=str(self.root / 'tmp'),
                        MWNET_CONTENT_DATA_DIR='', MWNET_CONTENT_FILES='',
                        MWNET_GAMEPLAY_TEST='0', OPENMW_DISABLE_CRASH_CATCHER='1')
        self.receipt = {'schemaVersion': 1, 'scope': 'isolated native backup/restore/rollback acceptance',
                        'runtimeSourceCommit': args.expected_commit, 'outcome': 'running',
                        'liveDeploymentTouched': False, 'gameAssetsRequired': False, 'checks': {}}
        proof = json.loads(args.native_receipt.read_text())
        if proof.get('sourceCommit') != args.expected_commit or proof.get('outcome') != 'pass':
            raise ValueError('Native build proof does not match candidate')
        for name in ('mwnet-directory', 'mwnet-persistence-fault', 'mwnet-discovery-tests', 'mwnet-headless-integration'):
            if digest(args.native_build / name) != proof['binaries'][name]['sha256']:
                raise ValueError('Native binary differs from source-qualified build proof')
        manifest = json.loads((args.package_root / 'manifest.json').read_text())
        if (manifest['runtimeSourceCommit'] != args.expected_commit
                or digest(args.server) != manifest['files']['build/mwnet-server']):
            raise ValueError('Packaged server differs from candidate manifest')
        self.receipt['nativeBuildProofSha256'] = digest(args.native_receipt)
        fixture = json.loads(args.fixture_receipt.read_text())
        if fixture.get('outcome') != 'pass' or fixture.get('expectedCommit') != args.expected_commit:
            raise ValueError('Fixture must come from accepted exact-candidate rendered gameplay')
        self.fixture_fingerprint = fixture['fingerprint']
        self.receipt['sourceFixtureReceiptSha256'] = digest(args.fixture_receipt)
        self.receipt['procedureSha256'] = {name: digest(Path(__file__).with_name(name)) for name in
                ('run_mwnet_recovery.py', 'mwnet_recovery_snapshot.py', 'run_mwnet_recovery_container.sh')}

    def command(self, args, label, *, env=None, cwd=None, expected=0, timeout=60):
        log = self.root / (label + '.log')
        with log.open('wb') as stream:
            log.chmod(0o600)
            result = subprocess.run(list(map(str, args)), cwd=cwd or self.root,
                                    env=env or self.env, stdout=stream,
                                    stderr=subprocess.STDOUT, timeout=timeout)
        if log.stat().st_size > 8 * 1024 * 1024:
            raise RuntimeError(label + ': log exceeded limit')
        if (expected == 0 and result.returncode != 0) or (expected != 0 and result.returncode == 0):
            raise RuntimeError(label + ': unexpected exit; see private log')
        return log.read_text(errors='replace')

    def expected(self, profile, label):
        data = profile / 'server/data'
        value = {'world': json.loads((data / 'world/world.json').read_text()),
                 'players': {p.stem: json.loads(p.read_text()) for p in (data / 'player').glob('*.json')}}
        if not value['players']:
            raise ValueError('Fixture must contain native-created characters')
        path = self.root / (label + '-expected.json')
        private_write(path, json.dumps(value))
        return path, len(value['players'])

    def probe(self, source, binary, commit, label, *, create_old=False):
        target = self.root / (label + '-probe')
        shutil.copytree(source, target)
        config = target / 'xdg-config/openmw/mwnet-server.cfg'
        config.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        private_write(config, '[General]\nlocalAddress = 127.0.0.1\npublicListen = false\n'
                      f'port = {port(socket.SOCK_DGRAM)}\nmaximumPlayers = 2\nlogLevel = 1\n'
                      f'\n[Plugins]\nhome = {target / "server"}\nplugins = serverCore.lua\n'
                      '\n[Discovery]\nannounce = false\n')
        scripts = target / 'server/scripts'
        private_write(scripts / 'recovery_probe.lua', LUA_PROBE)
        with (scripts / 'customScripts.lua').open('a') as stream:
            stream.write('\nrequire("recovery_probe")\n')
        env = dict(self.env, XDG_CONFIG_HOME=str(target / 'xdg-config'),
                   XDG_DATA_HOME=str(target / 'xdg-data'), XDG_CACHE_HOME=str(target / 'xdg-cache'),
                   HOME=str(target), LD_LIBRARY_PATH=str(binary.parent / 'lib'))
        if create_old:
            env['MWNET_RECOVERY_CREATE_OLD'] = '1'
        else:
            expected, _ = self.expected(source, label)
            env['MWNET_RECOVERY_EXPECTED'] = str(expected)
        text = self.command([binary, '--no-logs'], label, env=env, cwd=target)
        marker = 'MWNET_RECOVERY_OLD_CREATED' if create_old else 'MWNET_RECOVERY_READBACK_OK'
        if (marker not in text or 'Quitting peacefully.' not in text or 'Error state: false' not in text
                or f'Build commit: {commit[:10]}' not in text
                or any(x in text for x in ('Lua error:', 'stack traceback:', 'Error state: true'))):
            raise RuntimeError(label + ': native readback/source/shutdown not accepted; see private log')
        match = re.search(r'Server identity fingerprint: (ed25519/[A-Za-z0-9_-]{43})', text)
        if not match:
            raise RuntimeError(label + ': missing native identity')
        return target, match.group(1)

    def game_restore(self):
        original = self.root / 'game-state'
        shutil.copytree(self.args.fixture_state, original)
        expected = inventory(original)
        _, players = self.expected(original, 'initial')
        _, fingerprint = self.probe(original, self.args.server, self.args.expected_commit, 'baseline')
        assert fingerprint == self.fixture_fingerprint
        archive = self.root / 'game-state.tar.gz'
        archive_hash = snapshot(original, archive)
        # Simulate a broken attempted update in a disposable copy, retaining both generations.
        (original / 'server/data/world/world.json').write_text('{incomplete-write')
        (original / 'xdg-config/openmw/server-identity.key').write_bytes(b'corrupt identity')
        restored = self.root / 'game-restored'
        self.receipt['checks']['completeStateInventoryRestored'] = restore(archive, archive_hash, restored) == expected
        assert inventory(restored) == expected
        _, actual = self.probe(restored, self.args.server, self.args.expected_commit, 'restored')
        assert actual == fingerprint
        broken = self.root / 'truncated.tar.gz'
        broken.write_bytes(archive.read_bytes()[:128])
        try:
            restore(broken, archive_hash, self.root / 'bad-restored')
            raise AssertionError('Truncated backup accepted')
        except ValueError:
            assert not (self.root / 'bad-restored').exists()
        try:
            restore(archive, archive_hash, restored)
            raise AssertionError('Existing restore destination accepted')
        except ValueError:
            assert inventory(restored) == expected
        self.receipt['checks'].update(nativeCoreScriptsCharacterWorldReadback=True,
                                     serverIdentityPreserved=True, completeAccountCredentialFilesPreserved=True,
                                     corruptUpdateRetainedSeparately=True, truncatedBackupRefused=True,
                                     existingRestoreDestinationRefused=True)
        self.receipt['gameState'] = {'source': 'completed isolated rendered-game fixture',
                                    'files': len(expected['files']), 'bytes': expected['bytes'],
                                    'charactersReadThroughCoreScripts': players, 'archiveSha256': archive_hash,
                                    'graphicalGameplayReplayed': False, 'authenticationAfterRestoreReplayed': False}

    def directory_restore(self):
        database = self.root / 'directory.sqlite3'
        base = [self.args.directory, '--origin', 'https://recovery.test', '--database', database]
        self.command(base + ['--block-endpoint', '198.51.100.16:25565'], 'directory-seed')
        reader = sqlite3.connect(database)
        reader.execute('BEGIN')
        assert reader.execute('SELECT COUNT(*) FROM blocks').fetchone()[0] == 1
        writer = sqlite3.connect(database)
        writer.execute('PRAGMA wal_autocheckpoint=0')
        writer.execute('INSERT INTO blocks VALUES(?,?)', ('endpoint', '198.51.100.17:25565'))
        writer.commit()
        assert Path(str(database) + '-wal').stat().st_size > 32
        backup = self.root / 'directory-backup.sqlite3'
        try:
            self.command(base + ['--backup', backup], 'directory-online-backup')
            with sqlite3.connect(backup) as restored:
                assert restored.execute('PRAGMA integrity_check').fetchone()[0] == 'ok'
                assert restored.execute('SELECT COUNT(*) FROM blocks').fetchone()[0] == 2
                assert restored.execute('PRAGMA user_version').fetchone()[0] == 1
        finally:
            writer.close()
            reader.close()
        # Restart the actual backend from the standalone backup and query private health.
        listen = port()
        log = self.root / 'directory-restored.log'
        with log.open('wb') as stream:
            p = subprocess.Popen([str(self.args.directory), '--origin', 'https://recovery.test',
                                  '--database', str(backup), '--bind', '127.0.0.1', '--port', str(listen)],
                                 env=self.env, stdout=stream, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline:
                    try:
                        with urllib.request.urlopen(f'http://127.0.0.1:{listen}/healthz', timeout=1) as r:
                            assert r.status == 200
                        break
                    except OSError:
                        if p.poll() is not None:
                            raise RuntimeError('Restored directory exited')
                        time.sleep(.1)
                else:
                    raise RuntimeError('Restored directory health deadline')
            finally:
                p.terminate()
                p.wait(timeout=10)
        corrupt = self.root / 'directory-corrupt.sqlite3'
        corrupt.write_bytes(b'incomplete database')
        self.command([self.args.directory, '--origin', 'https://recovery.test', '--database', corrupt,
                      '--backup', self.root / 'rejected.sqlite3'], 'directory-corrupt-refused', expected=1)
        newer = self.root / 'directory-newer.sqlite3'
        shutil.copy2(backup, newer)
        with sqlite3.connect(newer) as conn:
            conn.execute('PRAGMA user_version=999')
        self.command([self.args.directory, '--origin', 'https://recovery.test', '--database', newer,
                      '--backup', self.root / 'newer-rejected.sqlite3'], 'directory-new-schema-refused', expected=1)
        self.receipt['checks'].update(directoryNativeOnlineBackupIncludesCommittedWal=True,
                                     directoryStandaloneBackupIntegrity=True, directoryRestoredNativeHealth=True,
                                     corruptDatabaseRefused=True, unsupportedNewerSchemaRefused=True)

    def old_upgrade(self):
        if not self.args.old_archive:
            self.receipt['oldRuntimeMigration'] = {'outcome': 'not-run', 'reason': 'No verified old package supplied'}
            return
        assert digest(self.args.old_archive) == self.args.old_archive_sha256
        old = self.root / 'old-package'
        old.mkdir(mode=0o700)
        with tarfile.open(self.args.old_archive) as archive:
            for entry in archive:
                split = entry.name.split('/', 1)
                if len(split) != 2:
                    continue
                name = split[1]
                if not (name in ('build/mwnet-server', 'build/mwnet-server-default.cfg', 'build/openmw.cfg')
                        or name.startswith('files/mwnet/core-scripts/')):
                    continue
                if entry.isdir():
                    continue
                if not entry.isreg() or '..' in Path(name).parts or entry.size > 128 * 1024 * 1024:
                    raise ValueError('Unsafe old package entry')
                target = old / name
                target.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
                with archive.extractfile(entry) as source, target.open('xb') as destination:
                    shutil.copyfileobj(source, destination)
                target.chmod(0o700 if name == 'build/mwnet-server' else 0o600)
        seed = self.root / 'old-seed'
        scripts = old / 'files/mwnet/core-scripts'
        shutil.copytree(scripts, seed / 'server')
        data = seed / 'server/data'
        for name in ('cell', 'custom', 'map', 'player', 'recordstore', 'world'):
            (data / name).mkdir(exist_ok=True)
        for name in ('banlist.json', 'requiredDataFiles.json'):
            shutil.copy2(scripts / 'defaults' / name, data / name)
        old_state, old_fingerprint = self.probe(seed, old / 'build/mwnet-server', self.args.old_commit,
                                                'old-create', create_old=True)
        # Remove only the acceptance hook; the state was written by the real old runtime.
        hook = old_state / 'server/scripts/customScripts.lua'
        hook.write_text(hook.read_text().replace('\nrequire("recovery_probe")\n', '\n'))
        saved = self.root / 'old-state.tar.gz'
        value = snapshot(old_state, saved)
        rollback = self.root / 'old-rollback'
        restore(saved, value, rollback)
        _, rollback_fingerprint = self.probe(rollback, old / 'build/mwnet-server', self.args.old_commit, 'old-rollback-read')
        assert rollback_fingerprint == old_fingerprint
        upgrade = self.root / 'old-upgraded'
        restore(saved, value, upgrade)
        current_scripts = self.args.package_root / 'files/mwnet/core-scripts'
        for name in ('scripts', 'lib'):
            shutil.rmtree(upgrade / 'server' / name)
            shutil.copytree(current_scripts / name, upgrade / 'server' / name)
        _, upgraded_fingerprint = self.probe(upgrade, self.args.server, self.args.expected_commit, 'old-upgraded-read')
        assert upgraded_fingerprint == old_fingerprint
        self.receipt['oldRuntimeMigration'] = {'outcome': 'pass', 'fromCommit': self.args.old_commit,
                'toCommit': self.args.expected_commit, 'sourceArchiveSha256': self.args.old_archive_sha256,
                'actualOldRuntimeWroteWorldAndCharacter': True, 'newCoreScriptsReadbackMatchesOldData': True,
                'oldRuntimeRollbackReadbackPasses': True, 'serverIdentityPreserved': True,
                'scope': 'Synthetic world/character written by old native CoreScripts; no old-version graphical character creation, account registration or full gameplay reconnect is claimed.'}

    def run(self):
        self.game_restore()
        self.directory_restore()
        self.old_upgrade()
        text = self.command([self.args.native_build / 'mwnet-persistence-fault'], 'native-six-stage-fault')
        assert 'all 6 stages' in text
        text = self.command([self.args.native_build / 'mwnet-discovery-tests'], 'native-discovery-restore')
        assert 'restore, replay' in text
        self.receipt['checks'].update(nativeAtomicSixStageCrashRecovery=True,
                                     signedListingsRestoredHiddenUntilIdentityReverification=True)
        text = self.command([self.args.native_build / 'mwnet-headless-integration', '--clients', '2',
                             '--cycles', '1', '--state-dir', self.root / 'native-auth-state',
                             '--metrics-output', self.root / 'native-auth-metrics.json',
                             '--commit', self.args.expected_commit], 'native-legacy-auth-migration', timeout=120)
        assert 'passed' in text.lower()
        self.receipt['checks']['nativeSyntheticLegacyCredentialMigration'] = True
        assert all(self.receipt['checks'].values())
        self.receipt.update(outcome='pass', completedUtc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                            binaries={p.name: digest(p) for p in (self.args.server, self.args.directory,
                            self.args.native_build / 'mwnet-persistence-fault', self.args.native_build / 'mwnet-discovery-tests',
                            self.args.native_build / 'mwnet-headless-integration')})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ('fixture-state', 'fixture-receipt', 'server', 'directory', 'native-build', 'native-receipt', 'package-root', 'output'):
        parser.add_argument('--' + flag, required=True, type=Path)
    parser.add_argument('--expected-commit', required=True)
    parser.add_argument('--old-archive', type=Path)
    parser.add_argument('--old-archive-sha256')
    parser.add_argument('--old-commit', default='4a92d877b45a937686e38b8c3b55aab4964c9584')
    args = parser.parse_args()
    os.umask(0o077)
    run = Acceptance(args)
    try:
        run.run()
    except Exception as error:
        run.receipt.update(outcome='fail', errorType=type(error).__name__, error=str(error))
        raise
    finally:
        private_write(run.root / 'recovery-receipt.json', json.dumps(run.receipt, indent=2) + '\n')
    print('Native recovery acceptance passed; structured receipt excludes credentials and raw state')


if __name__ == '__main__':
    main()
