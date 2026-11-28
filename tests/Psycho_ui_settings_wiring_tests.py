#!/usr/bin/env python3
"""P3/S088: the settings page reads and writes through psycho.settings, and marks unwired controls.

The page runs on my engine with a stub `psycho.settings`. A wired control must show the value the
broker listed, send a change as {key, value: <JSON text>}, and a control the broker lists as unwired
must say which step owns it.
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
window.psycho={settings:{
  list:function(){return {values:{restore:false,search:"brave",homepage:"psychovault://newtab",spellUserWords:"psycho\\nvault",
    speedDial:[{title:"One",url:"https://one.test/"},{title:"Two",url:"https://two.test/"}],
    services:[{name:"Svc",url:"https://svc.test/"}], containers:[], sitePermissions:{"https://seen.test":{camera:"allow"}}},unwired:{}};},
  set:function(a){document.title="set:"+a.key+"="+a.value;}},
  history:{clear:function(){document.title="history-clear";}}};
psycho.data={usage:function(){return {rows:[{id:"history",label:"Browsing history",count:3,unit:"pages"},{id:"cookies",label:"Cookies",count:7,unit:"cookies"}]};},
  clear:function(a){document.title="clear:"+a.what;return {queued:true};}};
psycho.diagnostics={report:function(){return {text:"Tabs: 3\\nVault: locked\\n"};},copy:function(){document.title="diag-copy";return {queued:true};}};
psycho.app={openlog:function(){document.title="openlog";return {queued:true};},relaunch:function(){document.title="relaunch";return {queued:true};}};
psycho.downloads={pickdir:function(){document.title="pickdir";return {queued:true};}};
psycho.updates={check:function(){document.title="updates-check";return {queued:true};},status:function(){return {text:"You are up to date"};}};
var vaultState={exists:true,locked:true,count:0};
psycho.vault={
  status:function(){return {exists:vaultState.exists,locked:vaultState.locked,count:vaultState.count};},
  unlock:function(a){document.title="unlock:"+a.passphrase;
    if(a.passphrase!=="right pass phrase") throw new Error("BridgeError: bridge.invoke.refused: vault.passphrase: no");
    vaultState.locked=false;vaultState.count=1;return {ok:true};},
  create:function(a){document.title="create:"+a.passphrase;vaultState.exists=true;vaultState.locked=false;return {ok:true};},
  lock:function(){vaultState.locked=true;document.title="lock";return {ok:true};},
  list:function(){return {entries:[{site:"example.test",user:"me",note:""}]};},
  add:function(a){document.title="add:"+a.site+":"+a.user+":"+a.pass;return {ok:true};},
  remove:function(a){document.title="remove:"+a.site;return {ok:true};},
  reveal:function(a){return {password:"s3cret"};},
  copy:function(a){document.title="copy:"+a.site;return {queued:true};},
  generate:function(){return {password:"G3nerated"};}};
"""


def run(extra=(), tail="", page="psychovault://settings/"):
    with tempfile.TemporaryDirectory() as directory:
        directory = pathlib.Path(directory)
        script = directory / "settings_probe.js"
        # The stub must exist before the page script runs, so it goes first.
        script.write_text(STUB + (ROOT / "ui/settings.js").read_text(encoding="utf-8") + tail, encoding="utf-8")
        cmd = [str(ENGINE), "--input", str(ROOT / "ui/settings.html"), "--url", page,
               "--width", "1200", "--height", "900", "--out", str(directory / "o.png")]
        for name in ("tokens.css", "components.css"):
            cmd += ["--resource", f"psychovault://settings/{name}={ROOT / 'ui' / name}"]
        cmd += ["--resource", f"psychovault://settings/settings.js={script}"]
        cmd += list(extra)
        proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, encoding="utf-8", timeout=60)
        if proc.returncode:
            raise AssertionError(f"engine exit {proc.returncode}: {proc.stderr}")
        return json.loads(proc.stdout)


class SettingsWiring(unittest.TestCase):
    VAULT = "psychovault://settings/#passwords"

    def vault_press(self, button, tail="", extra=()):
        box = run(("--inspect", f"#{button}"), page=self.VAULT, tail=tail)["inspected"][0]["box"]
        x, y = int(box["x"] + box["w"] / 2), int(box["y"] + box["h"] / 2)
        return run(("--action", f"click:{x},{y}") + tuple(extra), page=self.VAULT, tail=tail)

    def test_listed_values_fill_the_controls(self):
        out = run(tail='''
document.title = document.getElementById("search").value + "|" + document.getElementById("restore").checked +
  "|" + document.getElementById("homepage").value;''')
        self.assertIsNone(out.get("diagnostics"))
        self.assertEqual(out["title"], "brave|false|psychovault://newtab")

    def test_a_change_is_sent_as_json_text(self):
        box = run(("--inspect", "#restore"))["inspected"][0]["box"]
        x, y = int(box["x"] + box["w"] / 2), int(box["y"] + box["h"] / 2)
        out = run(("--action", f"click:{x},{y}"))
        # The listed value was false, so the press turns it on.
        self.assertEqual(out["title"], "set:restore=true")

    def test_a_textarea_save_button_sends_what_is_in_the_box(self):
        page = "psychovault://settings/#spelling"
        box = run(("--inspect", "#spellWordsSave"), page=page)["inspected"][0]["box"]
        x, y = int(box["x"] + box["w"] / 2), int(box["y"] + box["h"] / 2)
        out = run(("--action", f"click:{x},{y}"), page=page,
                  tail='document.getElementById("spellUserWords").value = "a\\nb";')
        self.assertEqual(out["title"], 'set:spellUserWords="a\\nb"')

    def test_listed_textarea_text_is_shown(self):
        out = run(tail='document.title = document.getElementById("spellUserWords").value.length;')
        self.assertEqual(out["title"], "12")  # "psycho\nvault"

    def test_clear_history_asks_twice(self):
        page = "psychovault://settings/#privacy"
        box = run(("--inspect", "#clear"), page=page)["inspected"][0]["box"]
        press = f"click:{int(box['x'] + 10)},{int(box['y'] + box['h'] / 2)}"
        self.assertNotEqual(run(("--action", press), page=page)["title"], "history-clear")
        self.assertEqual(run(("--action", press, "--action", press), page=page)["title"], "history-clear")


    def test_vault_status_says_locked_and_a_wrong_passphrase_is_explained(self):
        tail = 'document.getElementById("vpw").value = "bad";'
        out = run(("--inspect", "#vaultStatus"), page=self.VAULT, tail=tail)
        self.assertEqual(out["inspected"][0]["text"], "Vault locked.")
        out = self.vault_press("vgo", tail, ("--inspect", "#save-status", "--inspect", "#vpw"))
        self.assertIn("does not match", out["inspected"][0]["text"])
        self.assertEqual(out["inspected"][1].get("value", ""), "bad")  # a refused passphrase stays for another try

    def test_the_right_passphrase_unlocks_lists_entries_and_clears_the_box(self):
        tail = 'document.getElementById("vpw").value = "right pass phrase";'
        out = self.vault_press("vgo", tail, ("--inspect", "#vaultStatus", "--inspect", ".vrow", "--inspect", "#vpw"))
        self.assertEqual(out["title"], "unlock:right pass phrase")
        self.assertEqual(out["inspected"][0]["text"], "Vault unlocked - 1 entry")
        self.assertTrue(out["inspected"][1]["found"])
        self.assertEqual(out["inspected"][2].get("value", ""), "")

    def test_generate_fills_the_password_box_and_save_sends_the_entry(self):
        tail = 'document.getElementById("eSite").value = "a.test"; document.getElementById("eUser").value = "u";'
        out = self.vault_press("eGen", tail, ("--inspect", "#ePass"))
        self.assertEqual(out["inspected"][0].get("value"), "G3nerated")

    def test_set_master_password_creates_the_vault(self):
        tail = 'document.getElementById("vnew").value = "a long passphrase";'
        self.assertEqual(self.vault_press("vset", tail)["title"], "create:a long passphrase")


    def press(self, page, button, tail="", extra=()):
        box = run(("--inspect", f"#{button}"), page=page, tail=tail)["inspected"][0]["box"]
        x, y = int(box["x"] + box["w"] / 2), int(box["y"] + box["h"] / 2)
        return run(("--action", f"click:{x},{y}") + tuple(extra), page=page, tail=tail)

    def test_data_table_lists_what_is_kept_and_clearing_asks_twice(self):
        page = "psychovault://settings/#privacy"
        out = run(("--inspect", "#dynDataUsage .vrow", "--inspect", "#dynDataUsage .btn"), page=page)
        self.assertTrue(out["inspected"][0]["found"])
        box = out["inspected"][1]["box"]
        x, y = int(box["x"] + box["w"] / 2), int(box["y"] + box["h"] / 2)
        once = run(("--action", f"click:{x},{y}"), page=page)["title"]
        self.assertFalse(once.startswith("clear:"), once)
        self.assertEqual(run(("--action", f"click:{x},{y}", "--action", f"click:{x},{y}"), page=page)["title"], "clear:history")

    def test_diagnostics_report_fills_the_box_and_copy_asks_the_broker(self):
        page = "psychovault://settings/#about"
        out = run(tail='document.title = document.getElementById("diagnosticsReport").value;', page=page)
        self.assertIn("Tabs: 3", out["title"])
        self.assertEqual(self.press(page, "diagnosticsCopy")["title"], "diag-copy")
        self.assertEqual(self.press(page, "openErrorLog")["title"], "openlog")

    def test_update_check_is_requested_and_the_token_controls_are_disabled(self):
        page = "psychovault://settings/#about"
        self.assertEqual(self.press(page, "updateCheckNow")["title"], "updates-check")
        out = run(tail='document.title = String(document.getElementById("updateToken").disabled);', page=page)
        self.assertEqual(out["title"], "true")

    def test_restart_asks_twice(self):
        page = "psychovault://settings/#privacy"
        box = run(("--inspect", "#relaunch"), page=page)["inspected"][0]["box"]
        x, y = int(box["x"] + 10), int(box["y"] + box["h"] / 2)
        self.assertNotEqual(run(("--action", f"click:{x},{y}"), page=page)["title"], "relaunch")
        self.assertEqual(run(("--action", f"click:{x},{y}", "--action", f"click:{x},{y}"), page=page)["title"], "relaunch")


    def test_speed_dial_editor_lists_rows_and_saves_the_whole_list(self):
        page = "psychovault://settings/#appearance"
        out = run(("--inspect", "#dynTiles .vrow"), page=page)
        self.assertTrue(out["inspected"][0]["found"])
        saved = self.press(page, "tileSave")["title"]
        self.assertEqual(saved, 'set:speedDial=[{"title":"One","url":"https://one.test/"},{"title":"Two","url":"https://two.test/"}]')

    def test_adding_a_service_row_and_saving_sends_it_too(self):
        page = "psychovault://settings/#appearance"
        out = self.press(page, "svcAdd", extra=())
        box = run(("--inspect", "#svcSave"), page=page)["inspected"][0]["box"]
        x, y = int(box["x"] + box["w"] / 2), int(box["y"] + box["h"] / 2)
        add = run(("--inspect", "#svcAdd"), page=page)["inspected"][0]["box"]
        ax, ay = int(add["x"] + add["w"] / 2), int(add["y"] + add["h"] / 2)
        out = run(("--action", f"click:{ax},{ay}", "--action", f"click:{x},{y}"), page=page)
        self.assertEqual(out["title"], 'set:services=[{"name":"Svc","url":"https://svc.test/"},{"name":"","url":""}]')

    def test_site_permissions_save_the_rules_the_selects_show(self):
        page = "psychovault://settings/#privacy"
        tail = 'document.getElementById("permOrigin").value = "https://example.test/path"; document.getElementById("permCamera").value = "deny";'
        out = self.press(page, "permSave", tail)
        self.assertTrue(out["title"].startswith("set:sitePermissions="), out["title"])
        self.assertEqual(json.loads(out["title"][len("set:sitePermissions="):]),
                         {"https://seen.test": {"camera": "allow"}, "https://example.test": {"camera": "deny"}})

    def test_the_download_folder_button_asks_the_window_for_its_picker(self):
        page = "psychovault://settings/#downloads"
        self.assertEqual(self.press(page, "downloadDirPick")["title"], "pickdir")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine")
    args, rest = parser.parse_known_args()
    if args.engine:
        ENGINE = pathlib.Path(args.engine)
    unittest.main(argv=[sys.argv[0]] + rest)
