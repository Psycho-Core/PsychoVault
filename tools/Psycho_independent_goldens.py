#!/usr/bin/env python3
"""Exact pixel goldens constructed from explicit CSS/canvas geometry.

These focused contracts are not engine-captured baselines or a conformance
suite. The hand-authored expected rectangles are documented next to each case.
"""
from __future__ import annotations
import argparse
import json
import pathlib
import struct
import subprocess
import sys
import zlib

from Psycho_golden import compare, decode_png, write_png

ROOT = pathlib.Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests/golden/fixtures/independent"
W, H = 160, 100
WHITE = (255, 255, 255, 255)
RED = (221, 34, 34, 255)
GREEN = (34, 170, 68, 255)
BLUE = (34, 51, 221, 255)
YELLOW = (255, 170, 0, 255)
TRACK = (225, 228, 231, 255)  # engine scrollbar track composited over white
THUMB = (113, 134, 154, 255)  # engine scrollbar thumb
BLEND = (34, 111, 144, 255)    # 50% green over blue, 8-bit alpha
GRAY = (119, 119, 119, 255)     # existing input border
CYAN = (30, 144, 210, 255)
ORANGE = (245, 158, 11, 255)

# Coordinates are x, y, width, height, color; later rectangles paint over
# earlier ones. All rectangles are integer-aligned and text-free so the same
# images can be compared across portable and Windows builds.
CASES = {
    "values": {"steps": ["S052", "S054"], "rects": [(0, 0, 120, 80, BLUE), (10, 10, 50, 20, RED), (10, 40, 50, 20, GREEN)]},
    "cascade": {"steps": ["S053"], "rects": [(0, 0, 40, 20, RED), (0, 20, 40, 20, YELLOW), (0, 40, 40, 20, BLUE)]},
    "box": {"steps": ["S052", "S054"], "rects": [(0, 0, 80, 20, RED), (0, 40, 80, 20, GREEN), (0, 60, 80, 20, BLUE), (5, 65, 35, 10, YELLOW)]},
    "flex": {"steps": ["S055"], "rects": [(0, 0, 50, 20, GREEN), (60, 0, 50, 20, RED), (0, 30, 50, 20, BLUE)]},
    "grid": {"steps": ["S056"], "rects": [(0, 0, 40, 20, RED), (45, 0, 30, 20, GREEN), (0, 25, 75, 30, BLUE)]},
    "position": {"steps": ["S054", "S057"], "rects": [(0, 0, 80, 60, BLUE), (20, 10, 50, 40, RED), (50, 30, 30, 30, GREEN), (130, 70, 20, 20, YELLOW)], "fixed": True},
    "effects": {"steps": ["S058", "S067"], "rects": [(0, 0, 80, 60, BLUE), (30, 20, 20, 20, RED), (50, 30, 20, 20, BLEND)], "effect": True},
    "scroll_initial": {"fixture": "scroll", "steps": ["S059"], "rects": [(0, 0, 40, 20, RED), (0, 20, 40, 20, GREEN), (73, 0, 7, 40, TRACK), (73, 0, 7, 20, THUMB)]},
    "scroll_wheel": {"fixture": "scroll", "steps": ["S059"], "actions": ["wheel:10,10,20"], "rects": [(0, 0, 40, 20, GREEN), (0, 20, 40, 20, BLUE), (73, 0, 7, 40, TRACK), (73, 10, 7, 20, THUMB)]},
    "media_1x": {"fixture": "media", "steps": ["S064"], "rects": [(0, 0, 40, 30, GREEN), (0, 30, 40, 30, BLUE)]},
    "media_2x": {"fixture": "media", "steps": ["S064"], "dpr": 2, "rects": [(0, 0, 40, 30, GREEN), (0, 30, 40, 30, YELLOW)]},
    "events_initial": {"fixture": "events", "steps": ["S053", "S060"], "rects": [(0, 0, 40, 30, RED), (0, 30, 40, 30, BLUE)]},
    "events_hover": {"fixture": "events", "steps": ["S053", "S060", "S066", "S067"], "actions": ["hover:10,10"], "rects": [(0, 0, 40, 30, GREEN), (0, 30, 40, 30, BLUE)], "damage": True},
    "events_click": {"fixture": "events", "steps": ["S060", "S066"], "actions": ["click:10,10"], "rects": [(0, 0, 40, 30, GREEN), (0, 30, 40, 30, YELLOW)], "title": "bubbled", "damage": True},
    "events_idle": {"fixture": "events", "steps": ["S066", "S067"], "actions": ["render"], "rects": [(0, 0, 40, 30, RED), (0, 30, 40, 30, BLUE)], "idle": True},
    "control_initial": {"fixture": "control", "steps": ["S061"], "rects": [(0, 0, 40, 30, RED)]},
    "control_checked": {"fixture": "control", "steps": ["S060", "S061"], "actions": ["click:10,10"], "title": "checked", "rects": [(0, 0, 40, 30, GREEN)]},
    "edit_initial": {"fixture": "edit", "steps": ["S062"], "rects": [(0, 0, 1, 1, GRAY), (0, 1, 40, 30, RED)]},
    "edit_input": {"fixture": "edit", "steps": ["S060", "S062"], "actions": ["click:0,0", "text:x"], "title": "x", "rects": [(0, 0, 1, 1, GRAY), (0, 1, 40, 30, GREEN)]},
    "image": {"steps": ["S063"], "image": True, "rects": [(0, 0, 40, 20, YELLOW), (10, 0, 10, 10, CYAN), (20, 0, 10, 10, ORANGE), (10, 10, 10, 10, ORANGE), (20, 10, 10, 10, CYAN)]},
    "motion_reduced": {"fixture": "motion", "steps": ["S065"], "reduced": True, "rects": [(0, 0, 40, 30, GREEN)]},
    "canvas": {"steps": ["S068"], "rects": [(0, 0, 80, 60, RED), (10, 10, 30, 20, GREEN), (40, 30, 20, 20, BLUE)], "canvas": True},
    "font_1x": {"fixture": "font", "steps": ["S069"], "rects": [(0, 0, 80, 30, RED)], "font": True},
    "font_2x": {"fixture": "font", "steps": ["S064", "S069"], "dpr": 2, "rects": [(0, 0, 80, 30, RED)], "font": True},
}


def expected_pixels(rectangles):
    pixels = bytearray(bytes(WHITE) * (W * H))
    for x, y, width, height, color in rectangles:
        if x < 0 or y < 0 or width < 0 or height < 0 or x + width > W or y + height > H:
            raise ValueError("expected rectangle outside image")
        if len(color) != 4 or any(channel < 0 or channel > 255 for channel in color):
            raise ValueError("invalid color")
        row = bytes(color) * width
        for yy in range(y, y + height):
            at = (yy * W + x) * 4
            pixels[at:at + width * 4] = row
    return bytes(pixels)


def check_frame(case, frame):
    if "title" in case and frame.get("title") != case["title"]:
        raise ValueError("event title did not match")
    comp = frame.get("compositor", {})
    if case.get("damage") and (frame.get("fullDamage") or not frame.get("damageTiles")):
        raise ValueError("expected localized damage")
    if case.get("idle") and (frame.get("fullDamage") or frame.get("damageTiles") != []):
        raise ValueError("unchanged frame should not produce damage")
    if case.get("fixed") and comp.get("fixedLayerCommands", 0) < 1:
        raise ValueError("fixed layer metadata missing")
    if case.get("effect") and comp.get("effectLayers", 0) < 1:
        raise ValueError("effect layer metadata missing")
    if case.get("canvas") and not any(c.get("kind") == 5 for c in frame.get("paint", [])):
        raise ValueError("canvas command missing")
    if case.get("image") and not any(c.get("kind") == 3 and c.get("url") == "https://independent.test/pixel.png"
                                      for c in frame.get("paint", [])):
        raise ValueError("selected image command missing")
    if case.get("font") and not any(c.get("kind") == 2 and "PsychoVault UI" in c.get("fontFamily", "") and
                                     c.get("fontWeight") == 600 and c.get("size") == 20
                                     for c in frame.get("paint", [])):
        raise ValueError("font family, size or weight missing")


def run(engine, outdir):
    outdir = outdir.resolve()
    if outdir == ROOT or ROOT / "tests" == outdir or ROOT / "tests" in outdir.parents:
        raise ValueError("output may not overwrite test sources or the repository root")
    outdir.mkdir(parents=True, exist_ok=True)
    report = {"schema": 1, "viewport": [W, H], "method": "hand-specified rectangles", "cases": []}
    for name, case in CASES.items():
        item = {"id": name, "steps": case["steps"], "result": "failed"}
        image, diff = outdir / (name + ".png"), outdir / (name + ".diff.png")
        image.unlink(missing_ok=True)
        diff.unlink(missing_ok=True)
        (outdir / (name + ".expected.png")).unlink(missing_ok=True)
        try:
            cmd = [str(engine), "--input", str(FIXTURES / (case.get("fixture", name) + ".html")),
                   "--url", "https://independent.test/fixture.html", "--width", str(W), "--height", str(H), "--out", str(image)]
            if case.get("dpr"): cmd.extend(["--dpr", str(case["dpr"])])
            if case.get("reduced"): cmd.append("--reduced-motion")
            if case.get("image"):
                cmd.extend(["--resource", f"https://independent.test/pixel.png={ROOT / 'tests/fixtures/p2/pixel.png'}"])
            for action in case.get("actions", []): cmd.extend(["--action", action])
            proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, encoding="utf-8", timeout=30)
            if proc.returncode or proc.stderr.strip():
                raise RuntimeError(f"renderer exit {proc.returncode}: {proc.stderr.strip()}")
            frame = json.loads(proc.stdout)
            if not isinstance(frame, dict): raise ValueError("invalid frame")
            check_frame(case, frame)
            width, height, actual = decode_png(image)
            if (width, height) != (W, H): raise ValueError(f"unexpected dimensions {width}x{height}")
            expected = expected_pixels(case["rects"])
            changed, diff_pixels = compare(expected, actual)
            item["changed_pixels"] = changed
            if changed:
                write_png(diff, W, H, diff_pixels)
                write_png(outdir / (name + ".expected.png"), W, H, expected)
                item["diff"] = str(diff)
                raise ValueError(f"{changed} changed pixels")
            item["result"] = "passed"
        except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired, zlib.error, struct.error) as exc:
            item["error"] = str(exc)
        report["cases"].append(item)
        print(f"{name}: {item['result']}" + (f" — {item['error']}" if 'error' in item else ""))
    failed = sum(item["result"] == "failed" for item in report["cases"])
    report["failed"] = failed
    (outdir / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"{len(CASES)-failed}/{len(CASES)} passed; report: {outdir / 'report.json'}")
    return 1 if failed else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=pathlib.Path, default=ROOT / "build" / ("pv-engine.exe" if sys.platform == "win32" else "pv-engine"))
    parser.add_argument("--outdir", type=pathlib.Path, default=ROOT / "build/independent-golden-results")
    args = parser.parse_args()
    return run(args.engine.resolve(), args.outdir)


if __name__ == "__main__":
    sys.exit(main())
