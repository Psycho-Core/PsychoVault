#!/usr/bin/env python3
"""Pin and verify the upstream Titanium CRX before giving it to upstream's bundler.

This stages the upstream companion extension only; it is not Dark Reader or Proton.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import urllib.request

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent


def download_verified(url, digest, max_bytes, destination):
    if not isinstance(url, str) or not url.startswith('https://github.com/'):
        raise ValueError('Untrusted initial asset URL')
    if not isinstance(digest, str) or len(digest) != 64 or any(c not in '0123456789abcdef' for c in digest):
        raise ValueError('Invalid expected SHA-256')
    if not isinstance(max_bytes, int) or max_bytes < 1 or max_bytes > 10 * 1024 * 1024:
        raise ValueError('Invalid size limit')
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    fd, temp = tempfile.mkstemp(prefix='.titanium-', dir=destination.parent)
    try:
        h = hashlib.sha256()
        with os.fdopen(fd, 'wb') as output, urllib.request.urlopen(url, timeout=60) as response:
            total = 0
            while True:
                block = response.read(65536)
                if not block:
                    break
                total += len(block)
                if total > max_bytes:
                    raise ValueError('Extension asset exceeds size limit')
                h.update(block)
                output.write(block)
        if h.hexdigest() != digest:
            raise ValueError('Extension SHA-256 mismatch')
        os.replace(temp, destination)
    finally:
        if os.path.exists(temp):
            os.unlink(temp)


def main():
    config = json.loads((HERE / 'dependencies.json').read_text(encoding='utf-8'))['titaniumExtension']
    asset = HERE / 'download-cache' / 'titanium.crx'
    download_verified(config['url'], config['sha256'], config['maxBytes'], asset)
    subprocess.run(['python3', str(ROOT / 'extensions' / 'bundle.py'), str(ROOT / 'extensions' / 'dist'), 'titanium', asset.as_uri()], check=True)
    print('Pinned Titanium companion extension staged; Chromium installation and signature handling still require testing.')


if __name__ == '__main__':
    main()
