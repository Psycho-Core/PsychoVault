#!/usr/bin/env python3
"""S058: gradients, checked from the CSS Images 3 rules rather than a baseline.

A stop list is interpolated in premultiplied space, so fading rgba(c, 1) to
rgba(c, 0) over a black page gives c * (1 - t) at position t. Expected values are
computed here from that rule; tolerance is 2 per channel for 8-bit rounding.
"""
from __future__ import annotations
import argparse
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from Psycho_golden import decode_png

ENGINE = ROOT / "build" / ("pv-engine.exe" if sys.platform == "win32" else "pv-engine")
W, H = 200, 100


def render(body):
    with tempfile.TemporaryDirectory() as directory:
        page = pathlib.Path(directory) / "p.html"
        page.write_text("<!doctype html><body style='margin:0;background:#000'>" + body, encoding="utf-8")
        out = pathlib.Path(directory) / "p.png"
        proc = subprocess.run([str(ENGINE), "--input", str(page), "--url", "https://paint.test/p.html",
                               "--width", str(W), "--height", str(H), "--out", str(out)],
                              cwd=ROOT, capture_output=True, text=True, encoding="utf-8", timeout=60)
        if proc.returncode or proc.stderr.strip():
            raise AssertionError(f"engine exit {proc.returncode}: {proc.stderr}")
        _, _, pixels = decode_png(out)
        return pixels


def at(pixels, x, y):
    i = (y * W + x) * 4
    return tuple(pixels[i:i + 3])


class GradientTests(unittest.TestCase):
    def near(self, got, want, tolerance=2):
        for g, w in zip(got, want):
            self.assertLessEqual(abs(g - w), tolerance, f"{got} != {want}")

    def test_linear_midpoint(self):
        pixels = render("<div style='width:200px;height:100px;background:linear-gradient(90deg,#000000,#ffffff)'></div>")
        self.near(at(pixels, 100, 50), (128, 128, 128), 3)
        self.near(at(pixels, 1, 50), (1, 1, 1), 3)
        self.near(at(pixels, 198, 50), (254, 254, 254), 3)

    def test_fade_to_transparent_keeps_its_colour(self):
        # rgba(56,189,248,1) -> rgba(56,189,248,0) over black: colour * (1 - t), never darker hue
        pixels = render("<div style='width:200px;height:100px;background:linear-gradient(90deg,rgba(56,189,248,1),rgba(56,189,248,0))'></div>")
        self.near(at(pixels, 100, 50), (28, 94, 124), 3)

    def test_stop_positions_and_unpositioned_stops(self):
        # #f00 until 25%, blue from 75%, linear between: at 50% the mix is half and half
        pixels = render("<div style='width:200px;height:100px;background:linear-gradient(90deg,#ff0000 25%,#0000ff 75%)'></div>")
        self.near(at(pixels, 10, 50), (255, 0, 0))
        self.near(at(pixels, 100, 50), (128, 0, 128), 3)
        self.near(at(pixels, 190, 50), (0, 0, 255))

    def test_radial_circle_and_ellipse(self):
        # closest-side circle at the centre of a 200x100 box: radius 50, so x = 100 +- 50 is the edge colour
        pixels = render("<div style='width:200px;height:100px;background:radial-gradient(circle closest-side at 50% 50%,#ffffff,#000000)'></div>")
        self.near(at(pixels, 100, 50), (255, 255, 255), 6)
        self.near(at(pixels, 125, 50), (128, 128, 128), 6)
        self.near(at(pixels, 160, 50), (0, 0, 0))
        # an ellipse closest-side has radii 100 and 50: half way along each axis is the same colour
        pixels = render("<div style='width:200px;height:100px;background:radial-gradient(ellipse closest-side at 50% 50%,#ffffff,#000000)'></div>")
        self.near(at(pixels, 150, 50), at(pixels, 100, 75), 6)

    def test_radial_centre_position(self):
        pixels = render("<div style='width:200px;height:100px;background:radial-gradient(circle farthest-side at 0% 0%,#ffffff,#000000 80%)'></div>")
        self.near(at(pixels, 1, 1), (255, 255, 255), 8)
        self.near(at(pixels, 150, 80), (0, 0, 0))  # distance 170 is past the 80% stop at 160

    def test_explicit_radii(self):
        # "ellipse 80px 40px at 50% 50%": white at the centre, black from 80px across or 40px down
        pixels = render("<div style='width:200px;height:100px;background:radial-gradient(ellipse 80px 40px at 50% 50%,#ffffff,#000000)'></div>")
        self.near(at(pixels, 100, 50), (255, 255, 255), 6)
        self.near(at(pixels, 140, 50), (128, 128, 128), 6)   # half of 80px along x
        self.near(at(pixels, 100, 70), (128, 128, 128), 6)   # half of 40px along y
        self.near(at(pixels, 190, 50), (0, 0, 0))
        self.near(at(pixels, 100, 95), (0, 0, 0))

    def test_corner_keyword_direction(self):
        # "to bottom right" on a square runs top-left to bottom-right: the other two corners meet at 50%
        pixels = render("<div style='width:100px;height:100px;background:linear-gradient(to bottom right,#ff0000,#0000ff)'></div>")
        self.near(at(pixels, 99, 0), at(pixels, 0, 99), 6)
        self.near(at(pixels, 50, 50), (128, 0, 128), 6)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", type=pathlib.Path, default=ENGINE)
    args, remaining = parser.parse_known_args()
    ENGINE = args.engine.resolve()
    unittest.main(argv=[sys.argv[0], *remaining])
