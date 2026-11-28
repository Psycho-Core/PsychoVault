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
psycho.ui.panel=function(a){document.title="panel:"+a.kind;};
psycho.bookmarks={add:function(){document.title="bm-add";},remove:function(a){document.title="bm-remove:"+a.url;}};
psycho.history={clear:function(){document.title="history-clear";}};
psycho.palette={run:function(a){document.title="palette-run:"+a.id;},close:function(){document.title="palette-close";}};
psycho.find={step:function(a){document.title="find-step:"+a.delta;},close:function(){document.title="find-close";}};
psycho.downloads={remove:function(a){document.title="dl-remove:"+a.id;},clear:function(){document.title="dl-clear";}};
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


def center(selector, index=0, tail=""):
    boxes = run(("--inspect", selector), tail=tail)["inspected"]
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

    def panel_state(self, kind, items):
        import json as _json
        panel = {"kind": kind, "items": items}
        return ("psychoChromeState({tabs:[{id:1,title:'One'}],activeId:1,address:'',panel:%s});" % _json.dumps(panel))

    HISTORY = [{"title": "Alpha", "url": "https://a.test/"}, {"title": "Beta", "url": "https://b.test/"}]

    def test_panel_lists_rows_and_a_press_navigates(self):
        tail = self.panel_state("history", self.HISTORY)
        out = run(("--inspect", "#panel", "--inspect", ".prow"), tail=tail + 'document.title=String(document.querySelectorAll(".prow").length);')
        self.assertEqual(out["title"], "2")
        panel = out["inspected"][0]["box"]
        self.assertEqual((panel["x"], panel["y"], panel["w"]), (1110, 84, 330))
        box = out["inspected"][1]["box"]
        self.assertEqual(run(("--action", f"click:{int(box['x']) + 40},{int(box['y']) + 12}"), tail=tail)["title"], "go:https://a.test/")

    def test_download_row_shows_a_progress_bar_only_while_arriving(self):
        items = [{"id": 1, "filename": "a.zip", "state": "downloading", "received": 250, "total": 1000},
                 {"id": 2, "filename": "b.zip", "state": "complete", "received": 500, "total": 500}]
        out = run(("--inspect", ".bar", "--inspect", ".fill"), tail=self.panel_state("downloads", items))
        bar, fill = out["inspected"]
        self.assertTrue(bar["found"] and fill["found"])
        self.assertEqual(bar["box"]["w"], 250)
        self.assertEqual(fill["box"]["w"], 62.5)  # 25% of 250
        self.assertEqual(run(("--inspect", ".bar"), tail=self.panel_state("downloads", items[1:]))["inspected"][0].get("found"), False)

    def test_panel_filter_narrows_the_rows(self):
        tail = self.panel_state("history", self.HISTORY)
        box = run(("--inspect", "#panel-filter"), tail=tail)["inspected"][0]["box"]
        out = run(("--action", f"click:{int(box['x']) + 20},{int(box['y']) + 10}", "--action", "text:bet"),
                  tail=tail + 'document.addEventListener("keyup",function(){document.title=String(document.querySelectorAll(".prow").length);});')
        # The engine has no key-up for typed text in the CLI, so read the rows directly.
        out = run(("--action", f"click:{int(box['x']) + 20},{int(box['y']) + 10}", "--action", "text:bet", "--inspect", ".prow"), tail=tail)
        self.assertEqual(len([i for i in out["inspected"] if i.get("found")]), 1)

    def test_bookmark_row_remove_button_calls_the_bridge(self):
        tail = self.panel_state("bookmarks", self.HISTORY)
        box = run(("--inspect", ".prow .rm"), tail=tail)["inspected"][0]["box"]
        out = run(("--action", f"click:{int(box['x'] + box['w'] / 2)},{int(box['y'] + box['h'] / 2)}"), tail=tail)
        self.assertEqual(out["title"], "bm-remove:https://a.test/")

    def test_clear_history_asks_first_then_clears(self):
        tail = self.panel_state("history", self.HISTORY)
        foot = run(("--inspect", "#panel-foot button"), tail=tail)["inspected"][0]["box"]
        press_foot = f"click:{int(foot['x'] + foot['w'] / 2)},{int(foot['y'] + foot['h'] / 2)}"
        # The press opens a confirm dialog; nothing is cleared yet.
        self.assertNotEqual(run(("--action", press_foot), tail=tail)["title"], "history-clear")
        confirm = run(("--action", press_foot, "--inspect", "#dlg-actions button:last-child"), tail=tail)["inspected"][0]["box"]
        out = run(("--action", press_foot, "--action", f"click:{int(confirm['x'] + confirm['w'] / 2)},{int(confirm['y'] + confirm['h'] / 2)}"), tail=tail)
        self.assertEqual(out["title"], "history-clear")

    def test_main_menu_opens_the_history_panel(self):
        x, y = center("#menu")
        out = run(("--action", f"click:{x},{y}", "--inspect", '[data-go="history"]'))
        item = out["inspected"][0]["box"]
        out = run(("--action", f"click:{x},{y}", "--action", f"click:{int(item['x'] + 20)},{int(item['y'] + 12)}"))
        self.assertEqual(out["title"], "panel:history")

    def test_star_bookmarks_the_page(self):
        x, y = center("#bookmark")
        self.assertEqual(run(("--action", f"click:{x},{y}"))["title"], "bm-add")

    def find_state(self, text="hello", count=3, index=1, panel=False):
        extra = ",panel:{kind:'history',items:[]}" if panel else ""
        return ("psychoChromeState({tabs:[{id:1,title:'One'}],activeId:1,address:''%s,"
                "find:{text:'%s',count:%d,index:%d}});" % (extra, text, count, index))

    def test_find_bar_shows_the_shell_state_and_its_buttons_step_and_close(self):
        tail = self.find_state()
        out = run(("--inspect", "#findbar", "--inspect", "#find-count"), tail=tail)
        bar = out["inspected"][0]["box"]
        self.assertEqual((bar["x"], bar["y"], bar["w"], bar["h"]), (1044, 92, 380, 44))  # 16px in from the right edge
        self.assertEqual(out["inspected"][1]["text"], "2 of 3")
        for selector, expected in (("#find-next", "find-step:1"), ("#find-prev", "find-step:-1"), ("#find-done", "find-close")):
            x, y = center(selector, tail=tail)
            self.assertEqual(run(("--action", f"click:{x},{y}"), tail=tail)["title"], expected)

    def test_find_bar_moves_left_of_an_open_panel_and_says_when_nothing_matches(self):
        out = run(("--inspect", "#findbar", "--inspect", "#find-count"), tail=self.find_state("zzz", 0, 0, True))
        self.assertEqual(out["inspected"][0]["box"]["x"], 714)
        self.assertEqual(out["inspected"][1]["text"], "No matches")

    def palette_state(self):
        import json as _json
        palette = {"text": "tab", "index": 1, "matches": [
            {"label": "New tab", "chord": "Ctrl+T", "id": "palette-new-tab"},
            {"label": "Close tab", "chord": "Ctrl+W", "id": "palette-close-tab"}]}
        return "psychoChromeState({tabs:[{id:1,title:'One'}],activeId:1,address:'',palette:%s});" % _json.dumps(palette)

    def test_palette_draws_the_shell_state_and_a_pressed_row_runs(self):
        tail = self.palette_state()
        out = run(("--inspect", "#palette-text", "--inspect", ".prow2.sel", "--inspect", ".prow2"), tail=tail)
        self.assertEqual(out["inspected"][0]["text"], "tab")
        self.assertIn("Close tab", out["inspected"][1]["text"])
        row = out["inspected"][2]["box"]
        self.assertEqual(run(("--action", f"click:{int(row['x']) + 40},{int(row['y']) + 10}"), tail=tail)["title"], "palette-run:palette-new-tab")

    def test_a_press_outside_the_palette_closes_it(self):
        self.assertEqual(run(("--action", "click:100,700"), tail=self.palette_state())["title"], "palette-close")

    def test_no_palette_means_no_backdrop(self):
        self.assertFalse(run(("--inspect", "#pscrim"))["inspected"][0].get("hasBox", False))

    OUTLINE = 9   # PaintCommand::Kind::Outline

    def node_paints(self, out, node, kind=None):
        return [c for c in out["paint"] if int(c.get("node", -1)) == node and (kind is None or int(c["kind"]) == kind)]

    def test_hover_tints_a_frame_button_and_leaving_removes_it(self):
        info = run(("--inspect", "#nav-reload"))["inspected"][0]
        node, box = info["node"], info["box"]
        x, y = int(box["x"] + box["w"] / 2), int(box["y"] + box["h"] / 2)
        rest = self.node_paints(run(), node, 0)
        hovered = self.node_paints(run(("--action", f"hover:{x},{y}")), node, 0)
        self.assertEqual(len(rest), 0)
        self.assertGreaterEqual(len(hovered), 1)
        self.assertEqual(len(self.node_paints(run(("--action", f"hover:{x},{y}", "--action", "hover:700,300")), node, 0)), 0)

    def test_window_close_hover_is_red(self):
        info = run(("--inspect", "#win-close"))["inspected"][0]
        box = info["box"]
        out = run(("--action", f"hover:{int(box['x'] + 10)},{int(box['y'] + 10)}"))
        colors = [int(c["color"]) & 0xffffff for c in self.node_paints(out, info["node"], 0)]
        self.assertIn(0xc42b1c, colors)

    def test_keyboard_focus_draws_a_visible_ring_and_pointer_focus_does_not(self):
        info = run(("--inspect", "#nav-reload"))["inspected"][0]
        node = info["node"]
        # Tab walks to a button and draws its ring; a mouse press on one does not.
        walked = run(("--action", "key:Tab"))
        rings = [c for c in walked["paint"] if int(c["kind"]) == self.OUTLINE]
        self.assertGreaterEqual(len(rings), 1)
        box = info["box"]
        pressed = run(("--action", f"click:{int(box['x'] + 10)},{int(box['y'] + 10)}"))
        self.assertEqual([c for c in pressed["paint"] if int(c["kind"]) == self.OUTLINE and int(c.get("node", -1)) == node], [])

    def test_rail_services_come_from_the_state_and_a_press_opens_one(self):
        tail = ("psychoChromeState({tabs:[{id:1,title:'One'}],activeId:1,address:'',"
                "services:[{name:'Alpha',url:'https://alpha.test/'},{name:'Beta',url:'https://beta.test/'}]});")
        out = run(("--inspect", ".svc"), tail=tail + 'document.title = String(document.querySelectorAll(".svc").length);')
        self.assertEqual(out["title"], "2")
        box = out["inspected"][0]["box"]
        pressed = run(("--action", f"click:{int(box['x'] + 20)},{int(box['y'] + 15)}"), tail=tail)
        self.assertEqual(pressed["title"], "new:https://alpha.test/")

    def test_hide_sidebar_removes_the_rail_and_the_top_bar_takes_its_width(self):
        tail = "psychoChromeState({tabs:[{id:1,title:'One'}],activeId:1,address:'',hideSidebar:true});"
        out = run(("--inspect", ".rail", "--inspect", ".top", "--inspect", ".statusbar"), tail=tail)
        self.assertFalse(out["inspected"][0].get("hasBox", False))
        self.assertEqual(out["inspected"][1]["box"]["x"], 0)
        self.assertEqual(out["inspected"][2]["box"]["x"], 0)

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
