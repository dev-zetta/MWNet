#!/usr/bin/env python3
"""Replace managed runtime code mounts without changing persistent state mounts."""
from copy import deepcopy
from pathlib import PurePosixPath
import re


def target(volume):
    if isinstance(volume, dict):
        return volume.get('target')
    if isinstance(volume, str):
        pieces = volume.split(':')
        return pieces[1] if len(pieces) >= 2 else None
    raise ValueError('Unsupported volume declaration')


def bind(source, destination):
    return {'type': 'bind', 'source': str(source), 'target': destination,
            'read_only': True, 'bind': {'create_host_path': False}}


def previous_root(value):
    if value is not None and not re.fullmatch(r'/srv/mwnet/releases/[0-9a-f]{40}', str(value)):
        raise ValueError('Previous release must be an explicitly verified source revision')
    return PurePosixPath(value) if value is not None else None


def game_overlay(configuration, release_root, previous_release_root=None):
    root = PurePosixPath(release_root)
    if not root.is_absolute() or '..' in root.parts:
        raise ValueError('Release code directory must be absolute')
    result = deepcopy(configuration)
    game = result['services']['game']
    retained = []
    replacements = {'/opt/mwnet': root / 'server-package',
                    '/opt/registration/entrypoint.py': root / 'entrypoint.py',
                    '/opt/registration/verify_listing.py': root / 'verify_listing.py',
                    '/tmp': root / 'tmp'}
    previous = previous_root(previous_release_root)
    for volume in game.get('volumes', []):
        destination = target(volume)
        if destination == '/opt/mwnet/build/mwnet-server':
            source = volume.get('source', '') if isinstance(volume, dict) else ''
            if (not isinstance(volume, dict) or volume.get('type') != 'bind'
                    or volume.get('read_only') is not True
                    or not source.startswith('/srv/mwnet/server-patches/')
                    or PurePosixPath(source).name != 'mwnet-server'):
                raise ValueError('Unrecognized existing server binary override')
        elif destination in replacements:
            permitted = {str(replacements[destination])}
            if previous is not None:
                permitted.add(str(previous / replacements[destination].relative_to(root)))
            if (not isinstance(volume, dict) or volume.get('type') != 'bind'
                    or volume.get('source') not in permitted
                    or volume.get('read_only') is not (destination != '/tmp')):
                raise ValueError('Unrecognized existing runtime code override')
        else:
            retained.append(volume)
    code = [bind(source, destination) for destination, source in replacements.items()]
    next(volume for volume in code if volume['target'] == '/tmp')['read_only'] = False
    game['volumes'] = retained + code
    return result


def game_base(configuration):
    result = deepcopy(configuration)
    game = result['services']['game']
    temporary = game.get('tmpfs', [])
    if not isinstance(temporary, list) or not all(isinstance(item, str) for item in temporary):
        raise ValueError('Unsupported tmpfs declaration')
    retained = [item for item in temporary if item.split(':', 1)[0] != '/tmp']
    if retained:
        game['tmpfs'] = retained
    else:
        game.pop('tmpfs', None)
    return result


def directory_overlay(configuration, binary, previous_release_root=None):
    if not PurePosixPath(binary).is_absolute():
        raise ValueError('Directory binary must be absolute')
    result = deepcopy(configuration)
    directory = result['services']['directory']
    previous = previous_root(previous_release_root)
    permitted = {str(binary)}
    if previous is not None:
        permitted.add(str(previous / 'mwnet-directory'))
    retained = []
    for volume in directory.get('volumes', []):
        if target(volume) == '/usr/local/bin/mwnet-directory':
            if (not isinstance(volume, dict) or volume.get('type') != 'bind'
                    or volume.get('source') not in permitted or volume.get('read_only') is not True):
                raise ValueError('Unrecognized existing directory binary override')
        else:
            retained.append(volume)
    directory['volumes'] = retained + [bind(binary, '/usr/local/bin/mwnet-directory')]
    return result
