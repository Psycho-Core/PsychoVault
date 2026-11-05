#!/usr/bin/env python3
"""I vendor the Proton VPN browser extension into my repository.

This is a build tool, not a packaging step: it downloads Proton's GPL-3.0
extension source at the pinned commit, builds it with their own webpack
toolchain, and places the compiled payload plus their license, unchanged, into
extensions/Psycho-VPN-Proton/. The result is committed, so my normal build
never needs Node.

License boundary: Proton's extension is GPL-3.0. It ships in its own folder as
a separate program my browser loads at run time - my own code keeps its own
license. The payload files keep Proton's names and copyright headers; only the
provenance note and this script are mine (Psycho_ prefix).
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
TARGET = ROOT / "extensions" / "Psycho-VPN-Proton"
PIN = "71d7d86f164a2ee8e50ce5a389c9e5cbe0fd6883"
REPO = "https://github.com/ProtonVPN/proton-vpn-browser-extension"
EXPECTED_ID = "jplgfhpmjnbigmhklmmbgecoobifkmpa"
ID_NIBBLES = "abcdefghijklmnop"


def stable_extension_id(manifest_key: str) -> str:
    # MV install ids have one standard derivation, used by every extension
    # host my API is compatible with: the first 16
    # bytes of the SHA-256 of the DER public key, nibbles mapped onto a..p.
    digest = hashlib.sha256(base64.b64decode(manifest_key)).digest()[:16]
    return "".join(ID_NIBBLES[b >> 4] + ID_NIBBLES[b & 15] for b in digest)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--skip-build", action="store_true",
                        help="reuse an existing /tmp build tree (local iteration only)")
    args = parser.parse_args()

    work = pathlib.Path(tempfile.gettempdir()) / f"proton-build-{PIN[:7]}"
    if not args.skip_build or not (work / "distribution").is_dir():
        tarball = pathlib.Path(tempfile.gettempdir()) / f"proton-{PIN[:7]}.tar.gz"
        if not tarball.is_file():
            print(f"downloading {REPO} at {PIN[:7]} from codeload")
            urllib.request.urlretrieve(
                f"https://codeload.github.com/ProtonVPN/proton-vpn-browser-extension/tar.gz/{PIN}",
                tarball)
        staging = pathlib.Path(tempfile.gettempdir()) / f"proton-staging-{PIN[:7]}"
        for path in (work, staging):
            if path.exists():
                shutil.rmtree(path)
        staging.mkdir(parents=True)
        subprocess.run(["tar", "xzf", str(tarball), "-C", str(staging)], check=True)
        extracted = next(staging.iterdir())
        shutil.move(str(extracted), str(work))
        shutil.rmtree(staging, ignore_errors=True)
        env = dict(os.environ, HUSKY="0")
        # Their lockfile has a ts-jest peer conflict against @babel/core 8; it is
        # a test-only dependency, so I let npm use the legacy resolution just for
        # the install. The build itself runs their own webpack production config.
        subprocess.run(["npm", "ci", "--legacy-peer-deps", "--no-audit", "--no-fund"],
                       cwd=work, check=True, env=env)
        subprocess.run(["npm", "run", "build"], cwd=work, check=True, env=env)

    distribution = work / "distribution"
    license_file = work / "LICENSE"
    if not distribution.is_dir() or not license_file.is_file():
        print("build did not produce the distribution folder or the license", file=sys.stderr)
        return 1

    manifest = json.loads((distribution / "manifest.json").read_text(encoding="utf-8"))
    extension_id = stable_extension_id(manifest["key"])
    if extension_id != EXPECTED_ID:
        print(f"derived id {extension_id} does not match the published {EXPECTED_ID}",
              file=sys.stderr)
        return 1

    if TARGET.exists():
        shutil.rmtree(TARGET)
    TARGET.mkdir(parents=True)
    for entry in distribution.iterdir():
        if entry.is_dir():
            shutil.copytree(entry, TARGET / entry.name)
        else:
            shutil.copy2(entry, TARGET / entry.name)
    shutil.copy2(license_file, TARGET / "LICENSE")

    provenance = f"""# Proton VPN extension - provenance

This folder contains the compiled payload of Proton VPN's browser extension. It
is a separate program under the GNU General Public License v3 (see LICENSE,
their license, shipped verbatim). My browser loads it as an extension at run
time; licenses stay on their own sides: nothing of mine becomes GPL, none of
their code is mixed into my sources.

- Upstream: {REPO}
- Pinned commit: {PIN}
- Payload version: {manifest.get("version")}
- Extension identity (from the manifest key): {extension_id}
- Build recipe: `npm ci --legacy-peer-deps` then `npm run build`
  (their own webpack production config; the `--legacy-peer-deps` flag only
  resolves a conflict inside their test-only dependency `ts-jest` and does not
  alter the payload).
- Build host here: {subprocess.run(["node", "--version"], capture_output=True, text=True).stdout.strip()}
  with npm ci against registry.npmjs.org.
- Modifications by me: none to the payload. The manifest key is theirs, so the
  extension answers to account.proton.me exactly as the store build does.

To refresh the payload against a newer upstream commit, edit the pin in
tools/Psycho_vendor_proton.py and run it:

    python tools/Psycho_vendor_proton.py
"""
    (TARGET / "Psycho_PROVENANCE.md").write_text(provenance, encoding="utf-8")
    print(f"vendored {manifest.get('name')} {manifest.get('version')} "
          f"({extension_id}) -> {TARGET}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
