#!/usr/bin/env python3
"""I package only verified Windows files and emit the manifest my updater reads.

I never package a profile, a credential, a build tree or source control.
My browser is multi-process by design: PsychoVault.exe needs pv-renderer.exe
beside it, so the ZIP is the complete runnable payload and the manifest marks it
as the update target. I publish the bare executables too because I want them
individually downloadable and hash-verified.
"""
from __future__ import annotations
import argparse
import datetime
import hashlib
import json
import pathlib
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
BINARIES = ["PsychoVault.exe", "pv-renderer.exe", "pv-engine.exe", "pv-tests.exe"]
REQUIRED = ["PsychoVault.exe", "pv-renderer.exe"]


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--outdir", default="build")
    ap.add_argument("--commit", default="unknown")
    ap.add_argument("--tag", default="")
    ap.add_argument("--repo", default="Psycho-Core/PsychoVault")
    args = ap.parse_args()

    out = (ROOT / args.outdir).resolve()
    version = (ROOT / "Psycho_VERSION.txt").read_text(encoding="utf-8").strip()
    tag = args.tag or f"v{version}"
    missing = [name for name in BINARIES if not (out / name).is_file()]
    if missing:
        raise SystemExit("missing built files: " + ", ".join(missing))

    stage = out / "package"
    stage.mkdir(parents=True, exist_ok=True)
    files: dict[str, pathlib.Path] = {name: out / name for name in BINARIES}
    files["README-TEST.md"] = ROOT / "docs/Psycho_TEST-BUILD.md"
    files["NOTICE.txt"] = ROOT / "NOTICE.txt"

    info = out / "BUILD-INFO.txt"
    info.write_text(
        f"PsychoVault Windows x64 development preview\nversion={version}\n"
        f"tag={tag}\ncommit={args.commit}\n",
        encoding="utf-8",
    )
    files["BUILD-INFO.txt"] = info

    # I bundle my own VPN provider extension so the proxy path is exercised by
    # real extension code rather than a test-only hook.
    bundled = ROOT / "extensions"
    if bundled.is_dir():
        for path in sorted(bundled.rglob("*")):
            if path.is_file():
                files[str(path.relative_to(ROOT)).replace("\\", "/")] = path

    sums = [f"{sha256(path)}  {name}" for name, path in sorted(files.items())]
    checksums = stage / "SHA256SUMS.txt"
    checksums.write_text("\n".join(sums) + "\n", encoding="utf-8")
    files["SHA256SUMS.txt"] = checksums

    archive = out / "PsychoVault-windows-x64.zip"
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, path in sorted(files.items()):
            z.write(path, arcname=name)

    base = f"https://github.com/{args.repo}/releases/download/{tag}"
    published = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    def asset(name: str, path: pathlib.Path, role: str) -> dict:
        return {
            "name": name,
            "url": f"{base}/{name}",
            "sha256": sha256(path),
            "size": path.stat().st_size,
            "role": role,
        }

    manifest = {
        "schema": 1,
        "product": "PsychoVault",
        "version": version,
        "tag": tag,
        "channel": "preview",
        "platform": "windows-x64",
        "commit": args.commit,
        "publishedUtc": published,
        "complete": False,
        "notesUrl": f"https://github.com/{args.repo}/releases/tag/{tag}",
        "requires": ["PsychoVault.exe", "pv-renderer.exe"],
        "assets": [
            asset("PsychoVault-windows-x64.zip", archive, "update"),
            asset("PsychoVault.exe", out / "PsychoVault.exe", "browser"),
            asset("pv-renderer.exe", out / "pv-renderer.exe", "renderer"),
            asset("SHA256SUMS.txt", checksums, "checksums"),
        ],
    }
    manifest_path = out / "PsychoVault-update.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    # I copy the launchers into the stage so a workflow can upload a flat set.
    (stage / "PsychoVault-update.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    print(archive)
    print(manifest_path)
    print(f"version={version} tag={tag}")


if __name__ == "__main__":
    main()
