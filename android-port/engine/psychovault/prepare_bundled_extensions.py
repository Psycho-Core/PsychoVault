#!/usr/bin/env python3
"""Stage the existing desktop Proton/Dark Reader bundles as Android APK assets.

This does not sign a CRX, change an extension ID, or claim the extensions work.
Proton's manifest key is preserved so Chromium derives the same extension ID.
"""
from pathlib import Path
import hashlib
import os
import json
import zipfile

HERE = Path(__file__).resolve().parent
REPO = Path(os.environ.get('PSYCHO_REPO_ROOT') or HERE.parent.parent.parent)
OUT = HERE / 'bundled'
BUNDLES = {'proton-vpn': 'proton-vpn', 'dark-reader': 'dark-reader'}


def stage(name, source):
    manifest = json.loads((source / 'manifest.json').read_text(encoding='utf-8'))
    if not (source / 'LICENSE').is_file():
        raise RuntimeError(f'{name}: licence missing')
    if name == 'proton-vpn' and not manifest.get('key'):
        raise RuntimeError('Proton manifest key missing; refusing to change its identity')
    OUT.mkdir(exist_ok=True)
    target = OUT / f'{name}.zip'
    digest = hashlib.sha256()
    with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED) as z:
        for path in sorted(source.rglob('*')):
            if not path.is_file():
                continue
            rel = path.relative_to(source).as_posix()
            data = path.read_bytes()
            z.writestr(rel, data)
            digest.update(rel.encode()); digest.update(b'\0'); digest.update(data)
    return {'name': name, 'version': manifest['version'], 'sha256': digest.hexdigest(),
            'files': len(zipfile.ZipFile(target).namelist())}


def main():
    records = [stage(name, REPO / 'app' / 'builtin' / folder) for name, folder in BUNDLES.items()]
    (OUT / 'manifest.json').write_text(json.dumps(records, indent=2) + '\n', encoding='utf-8')
    for r in records:
        print(f"Staged {r['name']} {r['version']} ({r['files']} files). No extension behaviour was tested.")


if __name__ == '__main__':
    main()
