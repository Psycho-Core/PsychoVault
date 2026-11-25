#!/usr/bin/env python3
"""S061: default checkbox, radio, range and select are painted from geometry.

Expected values come from the documented control geometry, not from a captured
baseline: a default checkbox/radio is 13x13, a checked box carries a centred mark
of 55% of its side, a range is 129px wide by default with a 4px track, an accent
fill up to the thumb and a 14px thumb at min + (max - min) * fraction, and a
select shows a chevron near its right edge. No text is rasterized, so the same
pixels are expected on every platform.
"""
from __future__ import annotations
import argparse
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tests/fixtures/p2/controls-default.html"
sys.path.insert(0, str(ROOT / "tools"))
from Psycho_golden import decode_png

ENGINE = ROOT / "build" / ("pv-engine.exe" if sys.platform == "win32" else "pv-engine")
WIDTH, HEIGHT = 300, 200
WHITE = bytes((255, 255, 255, 255))
ACCENT = bytes((0x1a, 0x73, 0xe8, 255))   # range fill and thumb
TRACK = bytes((0xc8, 0xcc, 0xd0, 255))    # range track beyond the thumb
DARK = bytes((0x11, 0x11, 0x11, 255))   # the default text colour, used for marks and the chevron


def render(extra=()):
    with tempfile.TemporaryDirectory() as directory:
        output = pathlib.Path(directory) / "controls.png"
        cmd = [str(ENGINE), "--input", str(FIXTURE), "--url", "https://controls.test/page.html",
               "--width", str(WIDTH), "--height", str(HEIGHT), "--out", str(output)]
        for selector in ("#r", "#c", "#c2", "#ra", "#s"):
            cmd.extend(["--inspect", selector])
        cmd.extend(extra)
        proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, encoding="utf-8", timeout=60)
        if proc.returncode or proc.stderr.strip():
            raise AssertionError(f"engine exit {proc.returncode}; diagnostics: {proc.stderr}")
        frame = json.loads(proc.stdout)
        w, h, pixels = decode_png(output)
        if (w, h) != (WIDTH, HEIGHT):
            raise AssertionError(f"unexpected image dimensions: {w}x{h}")
        boxes = {item["selector"]: item["box"] for item in frame["inspected"]}
        return boxes, pixels


def pixel(pixels, x, y):
    at = (int(y) * WIDTH + int(x)) * 4
    return bytes(pixels[at:at + 4])


class ControlContractTests(unittest.TestCase):
    def test_default_sizes(self):
        boxes, _ = render()
        for selector in ("#c", "#c2", "#ra"):
            self.assertEqual((boxes[selector]["w"], boxes[selector]["h"]), (13, 13), selector)
        self.assertEqual((boxes["#r"]["w"], boxes["#r"]["h"]), (200, 16))  # width from CSS

    def test_checked_state_is_visible(self):
        boxes, pixels = render()
        def center(selector):
            box = boxes[selector]
            return pixel(pixels, box["x"] + box["w"] / 2, box["y"] + box["h"] / 2)
        self.assertEqual(center("#c2"), WHITE, "an unchecked box is empty inside")
        self.assertEqual(center("#c"), DARK, "a checked box carries a mark")
        self.assertEqual(center("#ra"), DARK, "a checked radio carries a dot")
        box = boxes["#c"]  # the mark is 55% of the side: the pixel just inside the border stays white
        self.assertEqual(pixel(pixels, box["x"] + 1, box["y"] + 1), WHITE)

    def test_range_follows_its_value(self):
        boxes, pixels = render()
        box = boxes["#r"]
        track_y = box["y"] + box["h"] / 2 - 1
        # value 25 of 0..100: thumb left edge at 0.25 * (200 - 14) = 46.5
        self.assertEqual(pixel(pixels, box["x"] + 10, track_y), ACCENT, "filled before the thumb")
        self.assertEqual(pixel(pixels, box["x"] + 53, box["y"] + box["h"] / 2), ACCENT, "thumb centre")
        self.assertEqual(pixel(pixels, box["x"] + 150, track_y), TRACK, "bare track beyond the thumb")

    def test_select_has_a_chevron(self):
        boxes, pixels = render()
        box = boxes["#s"]
        top = box["y"] + box["h"] / 2 - 2
        # first chevron row is 9px wide centred 12px from the right edge
        self.assertEqual(pixel(pixels, box["x"] + box["w"] - 12, top), DARK)
        self.assertEqual(pixel(pixels, box["x"] + box["w"] - 12, top - 4), WHITE)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", type=pathlib.Path, default=ENGINE)
    args, remaining = parser.parse_known_args()
    ENGINE = args.engine.resolve()
    unittest.main(argv=[sys.argv[0], *remaining])
