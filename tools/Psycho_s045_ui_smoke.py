#!/usr/bin/env python3
"""Exercise S045's app page and local web fixture in the owned headless engine.

This is deliberately a renderer proof, not the Windows application acceptance
run: it produces engine screenshots and verifies the returned layout frame.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]


def render(engine: pathlib.Path, html: pathlib.Path, url: str, output: pathlib.Path,
           width: int, height: int, resources: list[tuple[str, pathlib.Path]]) -> dict:
    command = [str(engine), "--input", str(html), "--url", url,
               "--width", str(width), "--height", str(height), "--out", str(output)]
    for resource_url, resource_path in resources:
        command.extend(["--resource", f"{resource_url}={resource_path}"])
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(f"owned engine failed for {html.name}:\n{result.stderr}\n{result.stdout}")
    try:
        frame = json.loads(result.stdout.strip().splitlines()[-1])
    except (IndexError, json.JSONDecodeError) as error:
        raise RuntimeError(f"owned engine did not return a frame for {html.name}: {error}")
    if not output.is_file() or output.stat().st_size < 100:
        raise RuntimeError(f"owned engine did not write a usable screenshot: {output}")
    if not isinstance(frame.get("paint"), list) or not frame["paint"]:
        raise RuntimeError(f"owned engine returned an empty paint list for {html.name}")
    return frame


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=pathlib.Path,
                        help="existing pv-engine binary (otherwise build it)")
    parser.add_argument("--outdir", type=pathlib.Path,
                        default=pathlib.Path("/tmp/PsychoVault-S045"),
                        help="directory for generated screenshots and report")
    parser.add_argument("--width", type=int, default=1396)
    parser.add_argument("--height", type=int, default=834)
    args = parser.parse_args()
    outdir = args.outdir.resolve()
    outdir.mkdir(parents=True, exist_ok=True)
    engine = args.engine.resolve() if args.engine else outdir / "build" / "pv-engine"
    if not args.engine:
        subprocess.run([sys.executable, "tools/Psycho_build.py", "--target", "engine",
                        "--compiler", "auto", "--outdir", str(engine.parent)],
                       cwd=ROOT, check=True)
    if not engine.is_file():
        raise RuntimeError(f"engine executable not found: {engine}")

    app_frame = render(
        engine, ROOT / "ui/index.html", "psychovault://newtab",
        outdir / "s045-app-ui.png", args.width, args.height,
        [("psychovault://newtab/app.js", ROOT / "ui/app.js")])
    local_frame = render(
        engine, ROOT / "ui/fixtures/local-page.html", "https://local.test/",
        outdir / "s045-local-web-fixture.png", args.width, args.height, [])
    if app_frame.get("title") != "PsychoVault — App UI Slice":
        raise RuntimeError(f"unexpected app UI document title: {app_frame.get('title')!r}")
    if local_frame.get("title") != "Local renderer fixture":
        raise RuntimeError(f"unexpected local fixture title: {local_frame.get('title')!r}")
    app_text = " ".join(" ".join(command.get("text", "") for command in app_frame["paint"]).split())
    local_text = " ".join(" ".join(command.get("text", "") for command in local_frame["paint"]).split())
    for phrase in ("Your", "browser,", "Open in tab", "Create a new tab"):
        if phrase not in app_text:
            raise RuntimeError(f"app UI screenshot is missing expected text: {phrase!r}")
    for phrase in ("LOCAL WEB FIXTURE", "same", "engine", "web", "document"):
        if phrase not in local_text:
            raise RuntimeError(f"local fixture screenshot is missing expected text: {phrase!r}")

    report = {
        "step": "S045 headless renderer slice",
        "result": "passed",
        "viewport": {"width": args.width, "height": args.height},
        "engine": str(engine),
        "app_ui": {"url": "psychovault://newtab", "title": app_frame["title"],
                   "paint_commands": len(app_frame["paint"]),
                   "screenshot": "s045-app-ui.png"},
        "local_web_fixture": {"url": "https://local.test/", "title": local_frame["title"],
                              "paint_commands": len(local_frame["paint"]),
                              "screenshot": "s045-local-web-fixture.png"},
        "limits": ["headless render proof only; not the Windows shell",
                   "production bridge IPC has separate pv-tests coverage, not this smoke",
                   "the Windows app transition and local-page flow remain unrun"],
    }
    (outdir / "s045-ui-smoke.json").write_text(json.dumps(report, indent=2) + "\n",
                                                 encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"S045 UI smoke failed: {error}", file=sys.stderr)
        raise SystemExit(1)
