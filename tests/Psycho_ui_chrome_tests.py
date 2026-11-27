#!/usr/bin/env python3
"""P3/S081: the browser frame page turns pushed state into a tab strip and presses into bridge calls.

The page is rendered by my own engine with a stub `psycho` object that records the call into the
document title. The engine reports the text node under the pointer as the click target, so a press
on a button's glyph must still reach the button (a regression found in the running shell).
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
STUB = """
window.psycho={tab:{close:function(a){document.title="close:"+a.id;},activate:function(a){document.title="activate:"+a.id;},
  new:function(a){document.title="new:"+a.url;}},nav:{back:function(){document.title="back";},go:function(a){document.title="go:"+a.url;}},
  win:{close:function(){document.title="win-close";}}};
psychoChromeState({tabs:[{id:1,title:"One"},{id:2,title:"Two"}],activeId:2,address:"https://example.test/",canBack:true,canForward:false});
"""


def run(extra=()):
    with tempfile.TemporaryDirectory() as directory:
        directory = pathlib.Path(directory)
        script = directory / "chrome_probe.js"
        script.write_text((ROOT / "ui/chrome.js").read_text(encoding="utf-8") + STUB, encoding="utf-8")
        cmd = [str(ENGINE), "--input", str(ROOT / "ui/chrome.html"), "--url", "psychovault://chrome/",
               "--width", "1440", "--height", "940", "--out", str(directory / "o.png")]
        for name in ("tokens.css", "components.css", "chrome.css"):
            cmd += ["--resource", f"psychovault://chrome/{name}={ROOT / 'ui' / name}"]
        cmd += ["--resource", f"psychovault://chrome/chrome.js={script}"]
        cmd += list(extra)
        proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, encoding="utf-8", timeout=60)
        if proc.returncode:
            raise AssertionError(f"engine exit {proc.returncode}: {proc.stderr}")
        return json.loads(proc.stdout)


def center(selector, index=0):
    boxes = run(("--inspect", selector))["inspected"]
    box = boxes[index]["box"]
    return int(box["x"] + box["w"] / 2), int(box["y"] + box["h"] / 2)


class ChromeFrame(unittest.TestCase):
    def test_state_draws_two_tabs_and_marks_one_active(self):
        result = run(("--inspect", "#tabs .tab", "--inspect", "#tabs .tab.active"))
        self.assertTrue(result["inspected"][0]["found"])
        self.assertTrue(result["inspected"][1]["found"])
        self.assertIsNone(result.get("diagnostics"))

    def test_close_glyph_closes_that_tab(self):
        x, y = center("#tabs .x")
        self.assertEqual(run(("--action", f"click:{x},{y}"))["title"], "close:1")

    def test_tab_body_activates_it(self):
        self.assertEqual(run(("--action", "click:100,20"))["title"], "activate:1")

    def test_plus_opens_a_new_tab(self):
        x, y = center("#new-tab")
        self.assertEqual(run(("--action", f"click:{x},{y}"))["title"], "new:psychovault://newtab")

    def test_back_button_calls_nav_back(self):
        x, y = center("#nav-back")
        self.assertEqual(run(("--action", f"click:{x},{y}"))["title"], "back")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine")
    args, rest = parser.parse_known_args()
    if args.engine:
        ENGINE = pathlib.Path(args.engine)
    unittest.main(argv=[sys.argv[0]] + rest)
