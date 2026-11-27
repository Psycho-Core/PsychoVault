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
psycho.tab.pin=function(a){document.title="pin:"+a.id;};
psycho.tab.mute=function(a){document.title="mute:"+a.id;};
psycho.ui={overlay:function(a){window.__overlay=a.open;},dialog:function(a){document.title="dialog:"+a.id+":"+a.button+":"+a.value;}};
psycho.rail={adblock:function(){document.title="adblock";}};
psychoChromeState({tabs:[{id:1,title:"One"},{id:2,title:"Two"}],activeId:2,address:"https://example.test/",canBack:true,canForward:false,status:"Loading https://example.test/"});
"""


def run(extra=(), tail=""):
    with tempfile.TemporaryDirectory() as directory:
        directory = pathlib.Path(directory)
        script = directory / "chrome_probe.js"
        script.write_text((ROOT / "ui/chrome.js").read_text(encoding="utf-8") + STUB + tail, encoding="utf-8")
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

    def test_state_replaces_the_tab_strip_instead_of_adding_to_it(self):
        out = run(tail='document.title = String(document.querySelectorAll("#tabs .tab").length);')
        self.assertEqual(out["title"], "2")  # the two tabs of the stub state, not those plus the static one

    def test_status_bar_shows_the_pushed_status(self):
        result = run(("--inspect", "#status-text", "--inspect", "#statusbar"))
        self.assertEqual(result["inspected"][0]["text"], "Loading https://example.test/")
        bar = result["inspected"][1]["box"]
        # 21px content plus the 1px top border is the 22px status bar of the reference chrome.
        self.assertEqual((bar["x"], bar["h"], bar["y"] + bar["h"]), (44, 21, 940))

    def test_right_click_opens_the_tab_menu_and_pin_calls_the_bridge(self):
        # Menu opens at the pointer; its first item (Pin tab) sits 6px padding + half a 30px row below.
        out = run(("--action", "contextmenu:100,20", "--action", "click:130,41"))
        self.assertEqual(out["title"], "pin:1")

    def test_menu_mute_and_close(self):
        self.assertEqual(run(("--action", "contextmenu:100,20", "--action", "click:130,71"))["title"], "mute:1")
        self.assertEqual(run(("--action", "contextmenu:100,20", "--action", "click:130,131"))["title"], "close:1")

    def test_press_elsewhere_closes_the_menu_without_acting(self):
        out = run(("--action", "contextmenu:100,20", "--action", "click:700,300"))
        self.assertEqual(out["title"], "PsychoVault")

    def test_rail_adblock_button_toggles_the_blocker(self):
        x, y = center("#rail-adblock")
        self.assertEqual(run(("--action", f"click:{x},{y}"))["title"], "adblock")

    def dialog_state(self, kind, buttons, message="Are you sure?"):
        import json as _json
        dialog = {"id": 7, "kind": kind, "title": "Confirm", "message": message, "buttons": buttons, "value": ""}
        return ("psychoChromeState({tabs:[{id:1,title:'One'}],activeId:1,address:'',dialog:%s});" % _json.dumps(dialog))

    def button_center(self, tail, index):
        boxes = run(("--inspect", "#dlg-actions button"), tail=tail)["inspected"]
        box = boxes[0]["box"] if index == 0 else None
        out = run(("--inspect", "#dlg-actions button:last-child"), tail=tail)["inspected"][0]["box"] if index else box
        return int(out["x"] + out["w"] / 2), int(out["y"] + out["h"] / 2)

    def test_dialog_is_drawn_over_the_page_and_ok_answers_it(self):
        tail = self.dialog_state("alert", ["OK"], "Hello from the page")
        out = run(("--inspect", "#dlg-message", "--inspect", "#scrim"), tail=tail)
        self.assertEqual(out["inspected"][0]["text"], "Hello from the page")
        scrim = out["inspected"][1]["box"]
        self.assertEqual((scrim["x"], scrim["y"], scrim["w"], scrim["h"]), (0, 0, 1440, 940))
        x, y = self.button_center(tail, 1)
        self.assertEqual(run(("--action", f"click:{x},{y}"), tail=tail)["title"], "dialog:7:0:")

    def test_confirm_buttons_report_which_was_pressed(self):
        tail = self.dialog_state("confirm", ["Cancel", "Delete"])
        x, y = self.button_center(tail, 0)
        self.assertEqual(run(("--action", f"click:{x},{y}"), tail=tail)["title"], "dialog:7:0:")
        x, y = self.button_center(tail, 1)
        self.assertEqual(run(("--action", f"click:{x},{y}"), tail=tail)["title"], "dialog:7:1:")

    def test_escape_answers_with_the_first_button(self):
        tail = self.dialog_state("confirm", ["Cancel", "Delete"])
        self.assertEqual(run(("--action", "key:Escape"), tail=tail)["title"], "dialog:7:0:")

    def test_tab_is_trapped_inside_the_dialog_and_enter_presses_the_focused_button(self):
        tail = self.dialog_state("confirm", ["Cancel", "Delete"])
        # Focus starts on the primary (last) button; Tab wraps to Cancel; Enter presses it.
        self.assertEqual(run(("--action", "key:Tab", "--action", "key:Enter"), tail=tail)["title"], "dialog:7:0:")
        # Two Tabs wrap all the way round to the primary button again.
        self.assertEqual(run(("--action", "key:Tab", "--action", "key:Tab", "--action", "key:Enter"),
                             tail=tail)["title"], "dialog:7:1:")

    def test_prompt_returns_the_typed_text(self):
        tail = self.dialog_state("prompt", ["Cancel", "OK"], "Name?")
        out = run(("--action", "text:abc", "--action", "key:Enter"), tail=tail)
        self.assertEqual(out["title"], "dialog:7:1:abc")

    def test_no_dialog_means_no_scrim(self):
        out = run(("--inspect", "#scrim"))
        self.assertFalse(out["inspected"][0].get("hasBox", False))

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
