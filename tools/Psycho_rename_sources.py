#!/usr/bin/env python3
"""I rename every file I authored so it starts with Psycho_, then I fix every reference.

I keep a small set of conventional names because tools and forges require them:
README.md, PSYCHO.md, NOTICE.txt, dotfiles, and the captured legacy evidence under
docs/reference/ which is identified by its original capture filenames.
"""
from __future__ import annotations
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
PREFIX = "Psycho_"
KEEP_NAMES = {"README.md", "PSYCHO.md", "NOTICE.txt"}
CODE_SUFFIXES = {".cpp", ".hpp", ".h", ".cc", ".py", ".ps1", ".bat", ".yml", ".yaml",
                 ".html", ".css", ".js", ".json", ".md", ".png", ".txt"}


def tracked() -> list[str]:
    return subprocess.check_output(["git", "ls-files"], cwd=ROOT, text=True).splitlines()


def exempt(path: str) -> bool:
    name = pathlib.PurePosixPath(path).name
    if name.startswith(PREFIX) or name.startswith("PsychoVault"):
        return True
    if name in KEEP_NAMES or name.startswith("."):
        return True
    # Captured legacy evidence keeps its original identity.
    if path.startswith("docs/reference/"):
        return True
    # My extension loader and every third-party extension need this exact name.
    if name == "manifest.json" and path.startswith("extensions/"):
        return True
    return pathlib.PurePosixPath(path).suffix not in CODE_SUFFIXES


def main() -> int:
    apply = "--apply" in sys.argv
    files = tracked()
    mapping: dict[str, str] = {}
    for path in files:
        if exempt(path):
            continue
        p = pathlib.PurePosixPath(path)
        mapping[path] = str(p.with_name(PREFIX + p.name))

    if not mapping:
        print("Nothing to rename. Every authored file already carries my prefix.")
        return 0

    print(f"I will rename {len(mapping)} authored files:")
    for old, new in sorted(mapping.items()):
        print(f"  {old} -> {new}")
    if not apply:
        print("\nDry run only. I pass --apply to perform the rename.")
        return 0

    for old, new in sorted(mapping.items()):
        subprocess.run(["git", "mv", old, new], cwd=ROOT, check=True)

    # I rewrite references in every tracked text file, longest path first so a
    # short basename never clobbers a longer qualified path.
    basename_map: dict[str, str] = {}
    for old, new in mapping.items():
        basename_map[pathlib.PurePosixPath(old).name] = pathlib.PurePosixPath(new).name

    ordered_paths = sorted(mapping.items(), key=lambda kv: -len(kv[0]))
    ordered_names = sorted(basename_map.items(), key=lambda kv: -len(kv[0]))

    rewritten = 0
    for path in tracked():
        target = ROOT / path
        if not target.is_file() or target.suffix in {".png", ".jpg", ".ico", ".bin"}:
            continue
        try:
            text = target.read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue
        original = text
        for old, new in ordered_paths:
            text = text.replace(old, new)
        # Bare basenames appear in #include "...", compiler source lists and logs.
        for old, new in ordered_names:
            text = re.sub(r"(?<!\w)" + re.escape(old), new, text)
        if text != original:
            target.write_text(text, encoding="utf-8")
            rewritten += 1
    print(f"\nI renamed {len(mapping)} files and rewrote references in {rewritten} files.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
