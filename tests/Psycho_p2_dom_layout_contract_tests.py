#!/usr/bin/env python3
"""Reference-derived S050 DOM/layout checks; not a full browser conformance suite."""
from __future__ import annotations
import argparse
import json
import pathlib
import subprocess
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests/fixtures/p2"
ENGINE = ROOT / "build" / ("pv-engine.exe" if sys.platform == "win32" else "pv-engine")


def render_frame(fixture, selectors, actions=(), resources=()):
    cmd = [str(ENGINE), "--input", str(FIXTURES / fixture), "--url", "https://s050.test/" + fixture,
           "--width", "1396", "--height", "834"]
    for selector in selectors:
        cmd.extend(["--inspect", selector])
    for action in actions:
        cmd.extend(["--action", action])
    for name in resources:
        cmd.extend(["--resource", f"https://s050.test/{name}={FIXTURES / name}"])
    proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, encoding="utf-8", timeout=60)
    if proc.returncode or proc.stderr.strip():
        raise AssertionError(f"engine exit {proc.returncode}; diagnostics: {proc.stderr}")
    return json.loads(proc.stdout)


def inspect(fixture, selectors, actions=(), resources=()):
    frame = render_frame(fixture, selectors, actions, resources)
    return {item["selector"]: item for item in frame["inspected"]}


def box(items, selector):
    item = items[selector]
    if not item["found"] or not item["hasBox"]:
        raise AssertionError(f"{selector} not found or not laid out: {item}")
    return item["box"]


class DomLayoutContractTests(unittest.TestCase):
    def test_chrome_reference_flex_and_grid_horizontal_tracks(self):
        # Chrome 157 on Windows, run 43703135149. Horizontal dimensions do not
        # depend on glyph rasterization; vertical metrics are intentionally not
        # claimed across engines/platforms.
        selectors = [".flex-row", *[f".card:nth-child({i})" for i in range(1, 4)],
                     ".grid", *[f".grid > div:nth-child({i})" for i in range(1, 9)]]
        items = inspect("flex-grid.html", selectors)
        expected = [(".flex-row", 16, 600), (".card:nth-child(1)", 16, 192),
                    (".card:nth-child(2)", 220, 192), (".card:nth-child(3)", 424, 192),
                    (".grid", 16, 520)]
        for index, (x, width) in enumerate([(16, 356), (378, 52), (436, 42), (484, 52)] * 2, 1):
            expected.append((f".grid > div:nth-child({index})", x, width))
        for selector, x, width in expected:
            with self.subTest(selector=selector):
                actual = box(items, selector)
                self.assertAlmostEqual(actual["x"], x, delta=0.1)
                self.assertAlmostEqual(actual["w"], width, delta=0.1)
        first = box(items, ".grid > div:nth-child(1)")
        second = box(items, ".grid > div:nth-child(5)")
        self.assertGreater(second["y"], first["y"])

    def test_external_script_mutates_real_target_with_body_default_margin(self):
        selectors = ["body", "#target", "#mutated"]
        items = inspect("dynamic-dom.html", selectors, resources=("fixture.js",))
        body, target, mutation = (box(items, selector) for selector in selectors)
        self.assertEqual((body["x"], body["w"]), (8, 1380))
        self.assertEqual((target["x"], target["w"]), (24, 1348))
        self.assertEqual((mutation["x"], mutation["w"]), (24, 1348))
        self.assertEqual(items["#target"]["text"], "Initial target marker")
        self.assertEqual(items["#mutated"]["text"], "External script mutation marker")
        self.assertGreater(mutation["y"], target["y"])

    def test_original_text_controls_flex_input_paints_and_edits_value(self):
        selectors = ["#address"]
        initial = render_frame("text-controls.html", selectors)
        item = initial["inspected"][0]
        self.assertTrue(item["found"] and item["hasBox"])
        bounds = item["box"]
        self.assertGreater(bounds["h"], 14)  # no longer only the padding and borders
        def text_runs(frame, node):
            return [c["text"] for c in frame["paint"] if c["kind"] == 2 and c["node"] == node]
        self.assertEqual(text_runs(initial, item["node"]), ["https://example.test/"])
        x, y = bounds["x"] + 2, bounds["y"] + bounds["h"] / 2
        updated = render_frame("text-controls.html", selectors,
                               actions=(f"click:{x},{y}", "key:End", "text:X"))
        self.assertEqual(text_runs(updated, item["node"]), ["https://example.test/X"])
        self.assertEqual(updated["inspected"][0]["value"], "https://example.test/X")

    def test_password_paint_never_contains_the_underlying_value(self):
        selectors = ["#inline", "#flex", "#hint"]
        frame = render_frame("Psycho_password_input.html", selectors)
        items = {item["selector"]: item for item in frame["inspected"]}
        def text(frame, name):
            node = items[name]["node"]
            return [c["text"] for c in frame["paint"] if c["kind"] == 2 and c["node"] == node]
        self.assertEqual(text(frame, "#inline"), ["•••"])
        self.assertEqual(text(frame, "#flex"), ["••••••"])
        self.assertEqual(text(frame, "#hint"), ["Enter password"])
        flex_box = box(items, "#flex")
        updated = render_frame("Psycho_password_input.html", selectors,
                               actions=(f"click:{flex_box['x'] + 2},{flex_box['y'] + flex_box['h'] / 2}",
                                        "key:End", "text:X"))
        self.assertEqual(text(updated, "#flex"), ["•••••••"])
        self.assertEqual(updated["inspected"][1]["value"], "secretX")

    def test_nested_scroll_and_position_anchors(self):
        selectors = [".shell", ".scroller", ".scroller p:nth-child(1)",
                     ".scroller p:nth-child(10)", ".badge", ".fixed-note"]
        before = inspect("scroll-position.html", selectors)
        after = inspect("scroll-position.html", selectors, actions=("wheel:30,200,80",))
        shell, badge, fixed = (box(before, s) for s in (".shell", ".badge", ".fixed-note"))
        self.assertAlmostEqual(badge["x"] + badge["w"], shell["x"] + shell["w"] - 1 - 12, delta=0.1)
        self.assertAlmostEqual(fixed["x"] + fixed["w"], 1396 - 12, delta=0.1)
        self.assertAlmostEqual(fixed["y"] + fixed["h"], 834 - 12, delta=0.1)
        self.assertLess(badge["w"], shell["w"])
        for selector in (".scroller p:nth-child(1)", ".scroller p:nth-child(10)"):
            self.assertAlmostEqual(box(after, selector)["y"], box(before, selector)["y"] - 80, delta=0.1)
        for selector in (".shell", ".scroller", ".badge", ".fixed-note"):
            self.assertAlmostEqual(box(after, selector)["y"], box(before, selector)["y"], delta=0.1)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", type=pathlib.Path, default=ENGINE)
    args, remaining = parser.parse_known_args()
    ENGINE = args.engine.resolve()
    unittest.main(argv=[sys.argv[0], *remaining])
