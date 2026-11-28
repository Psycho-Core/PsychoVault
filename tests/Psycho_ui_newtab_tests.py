#!/usr/bin/env python3
"""P3: the new-tab page shows the owner's speed-dial tiles when the broker supplies them.

The page runs on my engine with a stub `psycho.ui.tiles`; with no bridge the markup in the page stays.
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
STUB = 'window.psycho={ui:{tiles:function(){return {tiles:[{title:"Alpha",url:"https://alpha.test/"},{title:"Beta",url:"https://beta.test/"}]};}},tab:{new:function(a){document.title="new:"+a.url;}}};'


def run(stub=True, extra=()):
    with tempfile.TemporaryDirectory() as directory:
        directory = pathlib.Path(directory)
        script = directory / "app.js"
        script.write_text((STUB if stub else "") + (ROOT / "ui/app.js").read_text(encoding="utf-8"), encoding="utf-8")
        cmd = [str(ENGINE), "--input", str(ROOT / "ui/index.html"), "--url", "psychovault://newtab/", "--width", "1396",
               "--height", "834", "--out", str(directory / "o.png")]
        for name in ("tokens.css", "components.css"):
            cmd += ["--resource", f"psychovault://newtab/{name}={ROOT / 'ui' / name}"]
        cmd += ["--resource", f"psychovault://newtab/app.js={script}"] + list(extra)
        proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, encoding="utf-8", timeout=60)
        if proc.returncode:
            raise AssertionError(proc.stderr)
        return json.loads(proc.stdout)


class NewTab(unittest.TestCase):
    def test_tiles_come_from_the_broker(self):
        out = run(extra=("--inspect", ".tile", "--inspect", ".tile .lbl"))
        self.assertIsNone(out.get("diagnostics"))
        self.assertEqual(out["inspected"][1]["text"], "Alpha")

    def test_without_a_bridge_the_page_keeps_its_own_tiles(self):
        out = run(stub=False, extra=("--inspect", ".tile .lbl"))
        self.assertEqual(out["inspected"][0]["text"], "GitHub")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine")
    args, rest = parser.parse_known_args()
    if args.engine:
        ENGINE = pathlib.Path(args.engine)
    unittest.main(argv=[sys.argv[0]] + rest)
