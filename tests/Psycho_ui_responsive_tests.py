#!/usr/bin/env python3
"""P3/S090: my pages lay out cleanly across window sizes and display scales.

Renders the new-tab, settings and frame pages at three window sizes and device-pixel ratios 1, 1.25 and
1.5 and checks what must hold at every one: no script or layout diagnostics, nothing wider than the
window, the new-tab search box centred, and the frame's geometry (rail 44px, top bar 84px, status bar)
fixed in CSS pixels whatever the ratio.
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
ENGINE = ROOT / "build" / ("pv-engine.exe" if sys.platform == "win32" else "pv-engine")
SIZES = [(800, 600), (1366, 768), (1920, 1080)]
RATIOS = [1, 1.25, 1.5]
PAGES = {
    "newtab": ("index.html", "psychovault://newtab/", ["tokens.css", "components.css", "app.js"]),
    "settings": ("settings.html", "psychovault://settings/", ["tokens.css", "components.css", "settings.js"]),
    "chrome": ("chrome.html", "psychovault://chrome/", ["tokens.css", "components.css", "chrome.css", "chrome.js"]),
}


def render(page, width, height, ratio, inspect):
    html, url, resources = PAGES[page]
    host = url.split("//")[1].split("/")[0]
    with tempfile.TemporaryDirectory() as directory:
        cmd = [str(ENGINE), "--input", str(ROOT / "ui" / html), "--url", url, "--width", str(width),
               "--height", str(height), "--dpr", str(ratio), "--out", str(pathlib.Path(directory) / "o.png")]
        for name in resources:
            cmd += ["--resource", f"psychovault://{host}/{name}={ROOT / 'ui' / name}"]
        for selector in inspect:
            cmd += ["--inspect", selector]
        proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, encoding="utf-8", timeout=90)
        if proc.returncode:
            raise AssertionError(f"engine exit {proc.returncode}: {proc.stderr}")
        return json.loads(proc.stdout)


class Responsive(unittest.TestCase):
    def test_pages_render_without_diagnostics_at_every_size_and_ratio(self):
        for page in PAGES:
            for width, height in SIZES:
                for ratio in RATIOS:
                    with self.subTest(page=page, size=(width, height), ratio=ratio):
                        out = render(page, width, height, ratio, [])
                        self.assertIsNone(out.get("diagnostics"))

    def test_newtab_search_box_stays_inside_and_centred(self):
        for width, height in SIZES:
            for ratio in RATIOS:
                with self.subTest(size=(width, height), ratio=ratio):
                    box = render("newtab", width, height, ratio, [".search"])["inspected"][0]["box"]
                    self.assertGreaterEqual(box["x"], 0)
                    self.assertLessEqual(box["x"] + box["w"], width)
                    self.assertAlmostEqual(box["x"] + box["w"] / 2, width / 2, delta=1.5)

    def test_settings_cards_never_overflow_the_window(self):
        for width, height in SIZES:
            with self.subTest(size=(width, height)):
                out = render("settings", width, height, 1, [".card"])
                for item in out["inspected"]:
                    if item.get("box"):
                        self.assertLessEqual(item["box"]["x"] + item["box"]["w"], width + 1)

    def test_frame_geometry_is_fixed_in_css_pixels_at_every_ratio(self):
        for ratio in RATIOS:
            with self.subTest(ratio=ratio):
                out = render("chrome", 1440, 940, ratio, [".rail", ".top", ".toolbar", ".statusbar"])
                rail, top, toolbar, status = [i["box"] for i in out["inspected"]]
                self.assertEqual((rail["w"], top["h"], toolbar["y"], toolbar["h"]), (44, 84, 38, 46))
                self.assertEqual(status["y"] + status["h"], 940)

    def test_frame_stays_whole_at_the_smallest_window(self):
        out = render("chrome", 800, 500, 1, [".winctl", "#omnibox", ".top"])
        winctl, omnibox, top = [i["box"] for i in out["inspected"]]
        self.assertEqual(top["x"] + top["w"], 800)
        self.assertEqual(winctl["x"] + winctl["w"], 800)
        self.assertLess(omnibox["x"] + omnibox["w"], winctl["x"] + winctl["w"])
        self.assertGreater(omnibox["w"], 100)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine")
    args, rest = parser.parse_known_args()
    if args.engine:
        ENGINE = pathlib.Path(args.engine)
    unittest.main(argv=[sys.argv[0]] + rest)
