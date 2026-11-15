#!/usr/bin/env python3
"""Independent candidate-selection and rendered-pixel checks for img srcset."""
from __future__ import annotations
import argparse
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests/fixtures/p2"
sys.path.insert(0, str(ROOT / "tools"))
from Psycho_golden import decode_png

ENGINE = ROOT / "build" / ("pv-engine.exe" if sys.platform == "win32" else "pv-engine")
COLORS = {"responsive-red.png": bytes((221, 34, 34, 255)),
          "responsive-green.png": bytes((34, 170, 68, 255)),
          "responsive-blue.png": bytes((34, 51, 221, 255))}


def render(width, dpr):
    with tempfile.TemporaryDirectory() as directory:
        output = pathlib.Path(directory) / "image.png"
        cmd = [str(ENGINE), "--input", str(FIXTURES / "Psycho_responsive_images.html"),
               "--url", "https://s050.test/page.html", "--width", str(width), "--height", "60",
               "--dpr", str(dpr), "--out", str(output), "--inspect", "#density", "--inspect", "#width"]
        for name in COLORS:
            cmd.extend(["--resource", f"https://s050.test/{name}={FIXTURES / name}"])
        proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, encoding="utf-8", timeout=60)
        if proc.returncode or proc.stderr.strip():
            raise AssertionError(f"engine exit {proc.returncode}; diagnostics: {proc.stderr}")
        frame = json.loads(proc.stdout)
        w, h, pixels = decode_png(output)
        if (w, h) != (width, 60):
            raise AssertionError(f"unexpected image dimensions: {w}x{h}")
        return frame, pixels


class ResponsiveImageTests(unittest.TestCase):
    def test_descriptor_selection_and_actual_pixels(self):
        # Expected selection is computed from CSS viewport/density, not an
        # engine-generated baseline. The src fallback is the implicit 1x choice.
        scenarios = [(100, 1, "responsive-red.png", "responsive-red.png"),
                     (100, 2, "responsive-green.png", "responsive-red.png"),
                     (100, 3, "responsive-blue.png", "responsive-green.png"),
                     (300, 1, "responsive-red.png", "responsive-green.png"),
                     (300, 2, "responsive-green.png", "responsive-blue.png")]
        for width, dpr, density, sized in scenarios:
            with self.subTest(width=width, dpr=dpr):
                frame, pixels = render(width, dpr)
                commands = [item for item in frame["paint"] if item["kind"] == 3]
                self.assertEqual([item["url"] for item in commands],
                                 ["https://s050.test/" + density, "https://s050.test/" + sized])
                self.assertEqual([(item["x"], item["y"], item["w"], item["h"]) for item in commands],
                                 [(0, 0, 20, 20), (0, 20, 20, 20)])
                for y, name in ((10, density), (30, sized)):
                    at = (y * width + 10) * 4
                    self.assertEqual(pixels[at:at + 4], COLORS[name])


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", type=pathlib.Path, default=ENGINE)
    args, remaining = parser.parse_known_args()
    ENGINE = args.engine.resolve()
    unittest.main(argv=[sys.argv[0], *remaining])
