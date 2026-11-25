#!/usr/bin/env python3
"""S054/S055/S056: layout geometry checked against values worked out from CSS.

Each case states the box it expects from the CSS 2.1 / flexbox / grid rules, not
from a captured baseline. Boxes are read with the engine's --inspect.
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
BASE = "<style>*{box-sizing:border-box}body{margin:0}</style>"


def boxes(body, selectors, width=400, height=300, extra=()):
    with tempfile.TemporaryDirectory() as directory:
        page = pathlib.Path(directory) / "t.html"
        page.write_text(BASE + body, encoding="utf-8")
        cmd = [str(ENGINE), "--input", str(page), "--url", "https://layout.test/page.html",
               "--width", str(width), "--height", str(height),
               "--out", str(pathlib.Path(directory) / "t.png")]
        for selector in selectors:
            cmd.extend(["--inspect", selector])
        cmd.extend(extra)
        proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, encoding="utf-8", timeout=60)
        if proc.returncode or proc.stderr.strip():
            raise AssertionError(f"engine exit {proc.returncode}; diagnostics: {proc.stderr}")
        frame = json.loads(proc.stdout)
        return {item["selector"]: tuple(round(item["box"][k], 3) for k in "xywh")
                for item in frame["inspected"] if item.get("hasBox")}


class MarginCollapseTests(unittest.TestCase):
    def check(self, body, expected):
        got = boxes(body, list(expected))
        for selector, box in expected.items():
            self.assertEqual(got.get(selector), tuple(float(v) for v in box), selector)

    def test_first_child_margin_collapses_through_the_parent(self):
        self.check("<div id=p><div id=c style='margin-top:20px;height:10px'></div></div>",
                   {"#p": (0, 20, 400, 10), "#c": (0, 20, 400, 10)})

    def test_empty_block_margins_collapse_through_it(self):
        self.check("<div id=a style='height:10px'></div><div style='margin:15px 0'></div>"
                   "<div id=b style='margin-top:5px;height:10px'></div>", {"#b": (0, 25, 400, 10)})

    def test_last_child_margin_leaves_the_parent(self):
        self.check("<div id=p><div style='height:10px;margin-bottom:20px'></div></div>"
                   "<div id=n style='height:10px'></div>", {"#p": (0, 0, 400, 10), "#n": (0, 30, 400, 10)})

    def test_sibling_parent_and_child_collapse_to_the_largest(self):
        self.check("<div id=a style='margin-bottom:10px;height:10px'></div>"
                   "<div id=p><div id=c style='margin-top:30px;height:5px'></div></div>",
                   {"#p": (0, 40, 400, 5), "#c": (0, 40, 400, 5)})

    def test_positive_and_negative_margins_add(self):
        self.check("<div id=p style='margin-top:20px'><div id=c style='margin-top:-5px;height:10px'></div></div>",
                   {"#p": (0, 15, 400, 10), "#c": (0, 15, 400, 10)})

    def test_padding_border_overflow_and_flex_block_collapsing(self):
        self.check("<div id=p style='padding-top:1px'><div id=c style='margin-top:20px;height:10px'></div></div>",
                   {"#p": (0, 0, 400, 31), "#c": (0, 21, 400, 10)})
        self.check("<div id=p style='border-top:1px solid #000'><div id=c style='margin-top:20px;height:10px'></div></div>",
                   {"#p": (0, 0, 400, 31), "#c": (0, 21, 400, 10)})
        self.check("<div id=p style='overflow:hidden'><div id=c style='margin-top:20px;height:10px'></div></div>",
                   {"#p": (0, 0, 400, 30), "#c": (0, 20, 400, 10)})
        self.check("<div id=p style='display:flex'><div id=c style='margin-top:20px;height:10px;width:10px'></div></div>",
                   {"#p": (0, 0, 400, 30), "#c": (0, 20, 10, 10)})


class InlineTests(unittest.TestCase):
    def test_text_align_moves_inline_blocks_with_the_line(self):
        got = boxes("<div style='text-align:center'><span id=a style='display:inline-block;width:100px;height:10px'></span></div>"
                    "<div style='text-align:right'><span id=b style='display:inline-block;width:50px;height:10px'></span>"
                    "<span id=c style='display:inline-block;width:50px;height:10px'></span></div>"
                    "<div><span id=d style='display:inline-block;width:50px;height:10px'></span></div>",
                    ["#a", "#b", "#c", "#d"])
        self.assertEqual(got["#a"][0], 150)             # (400 - 100) / 2
        self.assertEqual((got["#b"][0], got["#c"][0]), (300, 350))  # a 100px line ends at 400
        self.assertEqual(got["#d"][0], 0)               # left-aligned stays put


class FlexGridTests(unittest.TestCase):
    def test_flex_shrink_distributes_the_overflow(self):
        got = boxes("<div style='display:flex;width:200px'><div id=a style='width:150px;height:10px'></div>"
                    "<div id=b style='width:150px;height:10px'></div></div>", ["#a", "#b"])
        self.assertEqual((got["#a"], got["#b"]), ((0, 0, 100, 10), (100, 0, 100, 10)))

    def test_grid_repeat_auto_rows_and_gap(self):
        got = boxes("<div style='display:grid;grid-template-columns:repeat(3,100px);grid-auto-rows:30px;gap:10px'>"
                    "<div id=a></div><div id=b></div><div id=c></div><div id=d></div><div id=e></div></div>",
                    ["#a", "#c", "#d", "#e"])
        self.assertEqual(got["#a"], (0, 0, 100, 30))
        self.assertEqual(got["#c"], (220, 0, 100, 30))
        self.assertEqual(got["#d"], (0, 40, 100, 30))
        self.assertEqual(got["#e"], (110, 40, 100, 30))

    def test_grid_lines_span_and_negative_lines(self):
        got = boxes("<div style='display:grid;grid-template-columns:1fr 1fr 1fr;width:300px'>"
                    "<div id=a style='grid-column:span 2;height:10px'></div><div id=b style='height:10px'></div>"
                    "<div id=c style='grid-column:1/-1;height:10px'></div>"
                    "<div id=d style='grid-column:2/4;height:10px'></div></div>", ["#a", "#b", "#c", "#d"])
        self.assertEqual(got["#a"], (0, 0, 200, 10))
        self.assertEqual(got["#b"], (200, 0, 100, 10))
        self.assertEqual(got["#c"], (0, 10, 300, 10))
        self.assertEqual(got["#d"], (100, 20, 200, 10))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", type=pathlib.Path, default=ENGINE)
    args, remaining = parser.parse_known_args()
    ENGINE = args.engine.resolve()
    unittest.main(argv=[sys.argv[0], *remaining])
