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
  list:function(){return {values:{restore:false,search:"brave",homepage:"psychovault://newtab",spellUserWords:"psycho\\nvault"},unwired:{}};},
  set:function(a){document.title="set:"+a.key+"="+a.value;}},
  history:{clear:function(){document.title="history-clear";}}};
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


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine")
    args, rest = parser.parse_known_args()
    if args.engine:
        ENGINE = pathlib.Path(args.engine)
    unittest.main(argv=[sys.argv[0]] + rest)
