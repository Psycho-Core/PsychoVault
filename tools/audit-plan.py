#!/usr/bin/env python3
"""I audit my owned-browser plan before implementation.

This is a planning audit, not a browser test. I check that my guide, research,
plan, inventory and reference screenshots agree with each other and that I have
not restored the old runtime into the new tree.
"""
from __future__ import annotations

import hashlib
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FAILURES: list[str] = []
PASSES: list[str] = []


def check(name: str, ok: bool, detail: str = "") -> None:
    if ok:
        PASSES.append(name)
        print(f"PASS {name}" + (f" - {detail}" if detail else ""))
    else:
        FAILURES.append(name)
        print(f"FAIL {name}" + (f" - {detail}" if detail else ""))


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def exists(rel: str) -> bool:
    return (ROOT / rel).exists()


def main() -> int:
    required = [
        "PSYCHO.md",
        "README.md",
        "docs/RESEARCH.md",
        "docs/PLAN.md",
        "docs/FEATURE-INVENTORY.md",
        "docs/reference/legacy-inventory.json",
        "docs/reference/legacy-ui/CAPTURE.json",
        "docs/reference/legacy-ui/README.md",
        ".github/workflows/plan-audit.yml",
    ]
    for rel in required:
        check(f"file exists: {rel}", exists(rel))

    inv = json.loads(read("docs/reference/legacy-inventory.json"))
    capture = json.loads(read("docs/reference/legacy-ui/CAPTURE.json"))
    plan = read("docs/PLAN.md")
    research = read("docs/RESEARCH.md")
    psycho = read("PSYCHO.md")
    feature = read("docs/FEATURE-INVENTORY.md")

    check("inventory source commit pinned", inv.get("sourceCommit") == "503a596bd89b5aaac979d4f6f86f8465e24c192d")
    check("capture source commit pinned", capture.get("sourceCommit") == "4a223b9c7e5d79a145564e059922d8d872c9d81c")
    check("ten settings categories", len(inv["settingsCategories"]) == 10, str(len(inv["settingsCategories"])))
    check("category ids match capture", [c["id"] for c in inv["settingsCategories"]] == [c["id"] for c in capture["categories"]])
    check("109 id controls", inv["settingsControlCount"] == 109)
    check("125 interactive settings controls", inv["interactiveSettingsControlCount"] == 125)
    check("59 default settings", inv["defaultSettingsCount"] == 59)
    check("105 IPC handlers", inv["mainIpcHandlerCount"] == 105)
    check("21 preload namespaces", len(inv["preloadNamespaces"]) == 21)
    check("23 screenshots", len(inv["screenshots"]) == 23)

    for cat in inv["settingsCategories"]:
        check(f"category documented: {cat['id']}", cat["title"] in feature and cat["id"] in plan)
        for control in cat["controls"]:
            check(f"control documented: {control['id']}", f"`{control['id']}`" in feature)
    for key in inv["defaultSettings"]:
        check(f"default documented: {key}", f"`{key}`" in feature)
    for handler in inv["mainIpcHandlers"]:
        check(f"IPC documented: {handler}", f"`{handler}`" in feature)

    for shot in inv["screenshots"]:
        p = ROOT / "docs/reference/legacy-ui" / shot["file"]
        ok = p.exists()
        if ok:
            data = p.read_bytes()
            ok = data[:8] == b"\x89PNG\r\n\x1a\n"
            ok = ok and hashlib.sha256(data).hexdigest() == shot["sha256"]
            ok = ok and len(data) == shot["bytes"]
            ok = ok and struct.unpack(">II", data[16:24]) == (shot["width"], shot["height"])
        check(f"screenshot verified: {shot['file']}", ok)

    for req in [f"R{i}" for i in range(1, 13)]:
        check(f"plan requirement present: {req}", re.search(rf"\|\s*{req}\s*\|", plan) is not None)
    for phase in [f"P{i}" for i in range(0, 10)]:
        check(f"plan phase present: {phase}", re.search(rf"\|\s*{phase}\s*\|", plan) is not None)

    pins = [
        "404b3eb14a7a936dddefb2d78e224dbeb01d2358",
        "3c53f93a3e1fb11ef5bd38029b7a9c97baaf0177",
        "845bafca22fb0c5a42c4bf384a9bdea525a2f10d",
        "59afe349913d56898c947576216f1c2579308be7",
        "4ad14cf8ec06c18e3ee8b20b0fa0683424b8b29a",
        "91c70759363c46557b694792bf34a2551a7af5ba",
        "fc3afa7b2f8ae4efe724da7f88f42b00499ce94c",
        "0e5338a1826ea666cce5a49ae67570bbb3f35d4e",
        "71d7d86f164a2ee8e50ce5a389c9e5cbe0fd6883",
    ]
    for pin in pins:
        check(f"research pin present: {pin[:12]}", pin in research)

    check("Proton permissions documented", all(p in research for p in ["webRequestAuthProvider", "asyncBlocking", "fixed_servers", "pac_script", "4443"]))
    check("plan rejects embedded browser engines", all(s in plan for s in ["Electron", "Chromium", "Gecko", "WebView"]))
    check("plan keeps Proton as standard extension", "standard bundled extension" in plan)
    check("plan keeps one heavy build", "one concurrency group for heavy builds" in plan)
    check("plan keeps Windows checkout path", "D:\\Blakes_SHIT\\#Psycho-core_REPOS\\PsychoVault" in plan)
    check("guide names PSYCHO.md", "PSYCHO.md" in psycho)
    check("guide requires own engine", "Every line of PsychoVault application and engine source" in psycho)

    workflow = read(".github/workflows/plan-audit.yml")
    check("plan audit workflow uses read-only permissions", "contents: read" in workflow)
    check("plan audit workflow runs audit script", "python3 tools/audit-plan.py" in workflow)
    check("plan audit workflow has bounded timeout", "timeout-minutes:" in workflow)

    forbidden = [
        "app/main.js",
        "app/preload.js",
        "app/package.json",
        "android-port",
        "extension-engine",
        "node_modules",
        "package-lock.json",
        "Cargo.lock",
        "target",
    ]
    for rel in forbidden:
        check(f"old/runtime path absent: {rel}", not exists(rel))

    md_files = [ROOT / "PSYCHO.md", ROOT / "README.md", *sorted((ROOT / "docs").rglob("*.md"))]
    em_dash = []
    bad_links = []
    for p in md_files:
        text = p.read_text(encoding="utf-8")
        if "\u2014" in text:
            em_dash.append(str(p.relative_to(ROOT)))
        for m in re.finditer(r"\[[^\]]+\]\(([^)]+)\)", text):
            target = m.group(1).split("#", 1)[0]
            if not target or re.match(r"^[a-z]+:", target):
                continue
            if not (p.parent / target).exists():
                bad_links.append(f"{p.relative_to(ROOT)} -> {target}")
    check("no em dashes in docs", not em_dash, ", ".join(em_dash))
    check("local markdown links resolve", not bad_links, "; ".join(bad_links[:5]))

    print(f"\n{len(PASSES)} passed, {len(FAILURES)} failed")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
