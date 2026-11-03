#!/usr/bin/env python3
"""I package only the verified Windows preview files, never a profile or build tree."""
from __future__ import annotations
import argparse, hashlib, pathlib, zipfile
ROOT = pathlib.Path(__file__).resolve().parents[1]
BINARIES = ["PsychoVault.exe", "pv-renderer.exe", "pv-engine.exe", "pv-tests.exe"]
def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--outdir", default="build")
    ap.add_argument("--commit", default="unknown")
    args = ap.parse_args()
    out = (ROOT / args.outdir).resolve()
    stage = out / "package"
    stage.mkdir(parents=True, exist_ok=True)
    missing = [name for name in BINARIES if not (out / name).is_file()]
    if missing:
        raise SystemExit("missing built files: " + ", ".join(missing))
    files: dict[str, pathlib.Path] = {name: out / name for name in BINARIES}
    files["README-TEST.md"] = ROOT / "docs/TEST-BUILD.md"
    files["NOTICE.txt"] = ROOT / "NOTICE.txt"
    info = out / "BUILD-INFO.txt"
    info.write_text(f"PsychoVault Windows x64 development preview\ncommit={args.commit}\n", encoding="utf-8")
    files["BUILD-INFO.txt"] = info
    sums = []
    for name, path in sorted(files.items()):
        data = path.read_bytes()
        sums.append(f"{hashlib.sha256(data).hexdigest()}  {name}")
    checksums = stage / "SHA256SUMS.txt"
    checksums.write_text("\n".join(sums) + "\n", encoding="utf-8")
    files["SHA256SUMS.txt"] = checksums
    archive = out / "PsychoVault-windows-x64-preview.zip"
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, path in sorted(files.items()):
            z.write(path, arcname=name)
    print(archive)
if __name__ == "__main__":
    main()
