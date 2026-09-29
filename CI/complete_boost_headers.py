#!/usr/bin/env python3
"""Complete an exported SDK's Boost headers using its exact upstream version."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import tarfile
import tempfile
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('prefix', type=Path, help='SDK triplet installation prefix')
    args = parser.parse_args()
    include = args.prefix / 'include'
    version_header = (include / 'boost/version.hpp').read_text()
    number = int(re.search(r'#define BOOST_VERSION\s+(\d+)', version_header).group(1))
    parts = (number // 100000, number // 100 % 1000, number % 100)
    version = '.'.join(map(str, parts))
    directory = 'boost_' + '_'.join(map(str, parts))
    url = f'https://archives.boost.io/release/{version}/source/{directory}.tar.gz'
    with urllib.request.urlopen(url + '.json', timeout=60) as response:
        expected = json.load(response)['sha256']
    with tempfile.TemporaryDirectory(prefix='mwnet-boost-') as tmp:
        work = Path(tmp)
        archive = work / 'boost.tar.gz'
        with urllib.request.urlopen(url, timeout=120) as response, archive.open('wb') as output:
            shutil.copyfileobj(response, output)
        with archive.open('rb') as stream:
            actual = hashlib.file_digest(stream, 'sha256').hexdigest()
        if actual != expected:
            raise RuntimeError('Boost source archive checksum mismatch')
        with tarfile.open(archive) as source:
            headers = [entry for entry in source.getmembers()
                       if entry.name.startswith(directory + '/boost/')]
            source.extractall(work, members=headers, filter='data')
        shutil.copytree(work / directory / 'boost', include / 'boost', dirs_exist_ok=True)
    for name in ('asio/ip/address.hpp', 'format.hpp'):
        if not (include / 'boost' / name).is_file():
            raise RuntimeError(f'Boost archive is missing {name}')
    print(f'Installed complete Boost {version} headers; archive SHA-256 {actual}')


if __name__ == '__main__':
    main()
