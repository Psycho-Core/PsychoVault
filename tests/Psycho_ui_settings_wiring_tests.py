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
  list:function(){return {values:{restore:false,search:"brave",homepage:"psychovault://newtab"},unwired:{remember:"S141-S145"}};},
  set:function(a){document.title="set:"+a.key+"="+a.value;}}};
"""


def run(extra=(), tail=""):
    with tempfile.TemporaryDirectory() as directory:
        directory = pathlib.Path(directory)
        script = directory / "settings_probe.js"
        # The stub must exist before the page script runs, so it goes first.
        script.write_text(STUB + (ROOT / "ui/settings.js").read_text(encoding="utf-8") + tail, encoding="utf-8")
        cmd = [str(ENGINE), "--input", str(ROOT / "ui/settings.html"), "--url", "psychovault://settings/",
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

    def test_unwired_control_names_its_owner(self):
        out = run(tail='''
var r = document.getElementById("remember");
document.title = r.getAttribute("data-unwired") + "|" + r.getAttribute("title");''')
        self.assertTrue(out["title"].startswith("S141-S145|Not connected to the profile yet"), out["title"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine")
    args, rest = parser.parse_known_args()
    if args.engine:
        ENGINE = pathlib.Path(args.engine)
    unittest.main(argv=[sys.argv[0]] + rest)
