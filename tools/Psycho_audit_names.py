#!/usr/bin/env python3
"""I audit that every file I authored carries my Psycho prefix and that no stale
reference to an old filename survives anywhere in my repository.

This is a naming and reference-integrity audit. It is not a security proof.
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

passed = 0
failed: list[str] = []


def check(name: str, ok: bool, detail: str = "") -> None:
    global passed
    if ok:
        passed += 1
        print("PASS", name, ("- " + detail) if detail else "")
    else:
        failed.append(name)
        print("FAIL", name, ("- " + detail) if detail else "")


def tracked() -> list[str]:
    return subprocess.check_output(["git", "ls-files"], cwd=ROOT, text=True).splitlines()


def exempt(path: str) -> str | None:
    name = pathlib.PurePosixPath(path).name
    if name.startswith(PREFIX) or name.startswith("PsychoVault"):
        return "already carries my prefix"
    if name in KEEP_NAMES:
        return "required conventional name"
    if name.startswith("."):
        return "tool-required dotfile"
    if path.startswith("docs/reference/"):
        return "captured legacy evidence keeps its capture identity"
    if name == "manifest.json" and path.startswith("extensions/"):
        return "the extension format requires this exact filename"
    if path.startswith("extensions/Psycho-VPN-") or path.startswith("extensions/PsychoVault-VPN-Bridge"):
        # Vendored third-party payloads keep their own names, licenses and
        # provenance; renaming their files would hide what they really are.
        return "vendored extension payload authored upstream"
    if pathlib.PurePosixPath(path).suffix not in CODE_SUFFIXES:
        return "not authored source"
    return None


def main() -> int:
    files = tracked()
    owned = []
    for path in files:
        reason = exempt(path)
        if reason is None:
            failed.append("unprefixed authored file: " + path)
            print("FAIL unprefixed authored file:", path)
        else:
            owned.append(path)
    check("every authored file carries my Psycho prefix", not any(
        f.startswith("unprefixed") for f in failed), f"{len(files)} tracked files reviewed")

    # I collect the historical names from the rename commit so I can prove no
    # reference to them is left behind in source, includes, build lists or CI.
    old_names: set[str] = set()
    try:
        log = subprocess.check_output(
            ["git", "log", "--diff-filter=R", "--name-status", "--format=%H", "-n", "40"],
            cwd=ROOT, text=True)
        for line in log.splitlines():
            if line.startswith("R"):
                parts = line.split("\t")
                if len(parts) == 3:
                    old, new = parts[1], parts[2]
                    ob, nb = pathlib.PurePosixPath(old).name, pathlib.PurePosixPath(new).name
                    if nb.startswith(PREFIX) and not ob.startswith(PREFIX):
                        old_names.add(ob)
    except subprocess.CalledProcessError:
        pass

    stale: list[str] = []
    include_missing: list[str] = []
    for path in files:
        target = ROOT / path
        if not target.is_file() or target.suffix in {".png", ".jpg", ".ico", ".bin"}:
            continue
        try:
            text = target.read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue
        for old in old_names:
            if re.search(r"(?<![\w])" + re.escape(old), text):
                stale.append(f"{path}: {old}")
        if target.suffix in {".cpp", ".hpp", ".h", ".cc"}:
            for inc in re.findall(r'#\s*include\s*"([^"]+)"', text):
                # My spelling words compile into a generated header under the
                # build directory, never into the tree: it exists only when a
                # build runs.
                if inc == "Psycho_embedded_words.hpp" and \
                   (ROOT / "platform/Psycho_words.txt").is_file():
                    continue
                # The real compiler searches the including file's directory
                # first, then the repository root, which is exactly how my
                # build script invokes it.
                if not (target.parent / inc).resolve().exists() and \
                   not (ROOT / inc).resolve().exists():
                    include_missing.append(f"{path}: {inc}")

    check("no stale reference to a pre-rename filename", not stale,
          "; ".join(stale[:8]) if stale else f"{len(old_names)} historical names checked")
    check("every local #include resolves to a real file", not include_missing,
          "; ".join(include_missing[:8]) if include_missing else "all quoted includes resolve")

    # Build lists and CI must name files that exist.
    build = (ROOT / "tools/Psycho_build.py").read_text(encoding="utf-8")
    listed = re.findall(r'"((?:engine|platform|shell|tools|tests)/[^"]+\.(?:cpp|hpp))"', build)
    missing = [s for s in listed if not (ROOT / s).exists()]
    check("every source file in my build list exists", not missing,
          "; ".join(missing) if missing else f"{len(listed)} entries")
    prefixed = [s for s in listed if pathlib.PurePosixPath(s).name.startswith(PREFIX)]
    check("every source file in my build list is prefixed", len(prefixed) == len(listed),
          f"{len(prefixed)}/{len(listed)}")

    for wf in sorted((ROOT / ".github/workflows").glob("*.yml")):
        check("workflow file is prefixed: " + wf.name, wf.name.startswith(PREFIX))
        body = wf.read_text(encoding="utf-8")
        for ref in re.findall(r"tools/[\w./-]+\.py", body):
            check("workflow tool reference exists: " + ref, (ROOT / ref).exists())

    # `near` cost me a hosted build: Windows headers define it as an empty macro, so
    # every check whose expectation was zero turned into a comma expression and
    # failed. I keep my test helpers clear of names the platform owns.
    tests_body = (ROOT / "tests/Psycho_test_main.cpp").read_text(encoding="utf-8")
    platform_macros = ["near", "far", "small", "interface", "min", "max", "IN", "OUT"]
    clashes = [m for m in platform_macros
               if re.search(r"\b(?:bool|static)[^;\n]*\b" + m + r"\s*\(", tests_body)]
    check("my test helpers avoid names the Windows headers own", not clashes,
          ", ".join(clashes) if clashes else "near/far/min/max checked")

    launcher = (ROOT / "PsychoVault_easy_build.bat").read_text(encoding="utf-8", errors="replace")
    check("double-click launcher still points at my prefixed script",
          "Psycho_start-build.ps1" in launcher)

    print(f"\n{passed} passed, {len(failed)} failed")
    if failed:
        print("\nFailures:")
        for f in failed:
            print(" -", f)
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
