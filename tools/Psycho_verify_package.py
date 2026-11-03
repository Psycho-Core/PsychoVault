#!/usr/bin/env python3
"""I verify my packaged release before anything is published.

I check that every asset my manifest names really exists, that its SHA-256 and
size match, and that my ZIP holds what my updater requires and nothing private.
I keep this as a real file so my development workflow can run the same check
before I ever try to publish.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import pathlib
import sys
import zipfile


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify(out: pathlib.Path) -> None:
    manifest_path = out / "PsychoVault-update.json"
    if not manifest_path.is_file():
        raise SystemExit(f"missing {manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    assert manifest["product"] == "PsychoVault", manifest.get("product")
    assert manifest["platform"] == "windows-x64", manifest.get("platform")
    for asset in manifest["assets"]:
        path = out / asset["name"]
        if not path.is_file():
            raise SystemExit(f"my manifest names {asset['name']} but {path} does not exist")
        digest = sha256(path)
        if digest != asset["sha256"]:
            raise SystemExit(f"hash mismatch for {asset['name']}")
        if path.stat().st_size != asset["size"]:
            raise SystemExit(f"size mismatch for {asset['name']}")
    archive = out / "PsychoVault-windows-x64.zip"
    with zipfile.ZipFile(archive) as z:
        names = set(z.namelist())
        for required in manifest["requires"]:
            if required not in names:
                raise SystemExit(f"my ZIP is missing {required}")
        if "extensions/PsychoVault-VPN-Bridge/manifest.json" not in names:
            raise SystemExit("my ZIP is missing my bundled VPN bridge")
        for name in names:
            if name.startswith(("build/", ".git/")) or "test-profile" in name:
                raise SystemExit(f"my ZIP must not contain {name}")
    print("My manifest hashes and ZIP contents verified.")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--outdir", default="build")
    args = ap.parse_args()
    root = pathlib.Path(__file__).resolve().parents[1]
    verify((root / args.outdir).resolve())
    sys.exit(0)


if __name__ == "__main__":
    main()
